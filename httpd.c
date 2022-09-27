/* J. David's webserver */
/* This is a simple webserver.
 * Created November 1999 by J. David Blackstone.
 * CSE 4344 (Network concepts), Prof. Zeigler
 * University of Texas at Arlington
 */
/* This program compiles for Sparc Solaris 2.6.
 * To compile for Linux:
 *  1) Comment out the #include <pthread.h> line.
 *  2) Comment out the line that defines the variable newthread.
 *  3) Comment out the two lines that run pthread_create().
 *  4) Uncomment the line that runs accept_request().
 *  5) Remove -lsocket from the Makefile.
 */
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>
// 函数说明：检查参数c 是否为空字符串，
// 也就是说判断是否为空格(' ')、定位字符('\t')、CR('\r')、换行('\n')、垂直定位符('\v')或者翻页('\f')的情况
// 返回值：若参数c为空白字符串，则返回非0，否则返回0
#define ISspace(x) isspace((int)(x))

#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n" //定义server的名称

void *accept_request(void*); //处理连接，子线程，处理从套接字上监听到的一个http请求
void bad_request(int); //400 返回给客户端这是个错误请求，响应码为400
void cat(int, FILE *); //处理文件，读取文件内容，并将文件写到socket套接字上返回给客户端
void cannot_execute(int); // 500 处理发生在执行cgi程序时出现的异常
void error_die(const char *); // 把错误信息写到perror
void execute_cgi(int, const char *, const char *, const char *); //调用cgi动态解析cgi脚本
int get_line(int, char *, int); //从缓存区读取一行http报文
void headers(int, const char *); //服务器成功响应，响应200
void not_found(int); //返回请求的资源不存在
void serve_file(int, const char *); //调用cat把服务器文件返回给浏览器
int startup(u_short *); //初始化服务器，包括端口绑定、监听、开启线程处理
void unimplemented(int); //501 返回给浏览器表明收到的http请求所用的method不支持

/**********************************************************************/
/* A request has caused a call to accept() on the server port to
 * return.  Process the request appropriately.
 * Parameters: the socket connected to the client */
/**********************************************************************/
//接受客户端连接，并读取请求数据
void *accept_request(void* pclient)
{
 char buf[1024];
 int numchars;
 char method[255];
 char url[255];
 char path[512];
 size_t i, j;
 struct stat st;
 int cgi = 0;      /* becomes true if server decides this is a CGI
                    * program */
 char *query_string = NULL;
size_t client = *(size_t *)pclient; //客户端描述符类型转换，主要针对mac和linux进行适配
//获取一行HTTP报文数据
 numchars = get_line(client, buf, sizeof(buf));
 i = 0; j = 0;
 //对于HTTP报文来说，第一行内容即为起始行，格式为 Method Request-URI version
 //每个字段用空格相连
 while (!ISspace(buf[j]) && (i < sizeof(method) - 1))
 {
  //提取其中请求方法是GET还是POST
  method[i] = buf[j];
  i++; j++;
 }
 method[i] = '\0';
//函数说明： strcasecmp() 用来比较参数s1和s2字符串，比较是会自动忽略大小写差异
//返回值： 若参数s1和s2字符串相同则返回0，s1长度大于s2的长度则返回大于0的值，如果s1长度小于s2的长度则返回小于0的值
 if (strcasecmp(method, "GET") && strcasecmp(method, "POST"))
 {
   //tinyhttp 仅实现了GET和POST请求，如果是其他请求方法则直接返回501
  unimplemented(client);
  return NULL;
 }
//cgi为标志位，1表示开启CGI解析
 if (strcasecmp(method, "POST") == 0)
  cgi = 1;//POST请求需要执行cgi脚本

 i = 0;
 //略过Method后面的空格
 while (ISspace(buf[j]) && (j < sizeof(buf)))
  j++;
//获取Request-URI
 while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf)))
 {
  url[i] = buf[j];
  i++; j++;
 }
 url[i] = '\0'; //在最后追加，标识字符串
//如果是GET请求，请求参数可以带有参数
 if (strcasecmp(method, "GET") == 0)
 {
  query_string = url;
  while ((*query_string != '?') && (*query_string != '\0'))
   query_string++;
  if (*query_string == '?')
  {
   cgi = 1; //带参数的GET请求需要开启CGI解析
   *query_string = '\0'; //截取解析参数
   query_string++;
  }
 }
//到此为止已经将起始行解析完成
//将url中的路径格式化到path中
 sprintf(path, "htdocs%s", url);
 if (path[strlen(path) - 1] == '/') //如果路径中最后一个字符为'/',则直接返回index.html
  strcat(path, "index.html");
  // 函数定义： int stat(const char *file_name,struct stat *buf)
  // 函数说明： 通过文件名filename获取文件信息，并保存在buf所指定的结构体stat中
  // 返回值： 执行成功则返回0，失败则返回-1，错误代码存储于errno(需要include <errno.h>)
 if (stat(path, &st) == -1) {
   //假如需要请求的资源不存在，则只需要重复读取身下的请求头以及丢弃即可
  while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
   numchars = get_line(client, buf, sizeof(buf));
  //最后声明网页不存在
  not_found(client);
 }
 else
 {
   //如果访问的网页存在，则需要进行处理
  if ((st.st_mode & S_IFMT) == S_IFDIR) //S_IFDIR代表目录
   strcat(path, "/index.html"); //如果是目录则需要凭据index.html
  if ((st.st_mode & S_IXUSR) || //文件所有者具有可执行权限
      (st.st_mode & S_IXGRP) ||  //用户组具有可执行权限
      (st.st_mode & S_IXOTH)    ) //其它用户具有可读权限
   cgi = 1;
  if (!cgi)
  //不需要进行CGI解析，直接返回静态资源
   serve_file(client, path);
  else
  //执行CGI动态解析
   execute_cgi(client, path, method, query_string);
 }

 close(client); //因为http是面向无连接的，所以需要关闭
 return NULL;
}

/**********************************************************************/
/* Inform the client that a request it has made has a problem.
 * Parameters: client socket */
/**********************************************************************/
void bad_request(int client)
{
 char buf[1024];
//发送400
 sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
 send(client, buf, sizeof(buf), 0);
 sprintf(buf, "Content-type: text/html\r\n");
 send(client, buf, sizeof(buf), 0);
 sprintf(buf, "\r\n");
 send(client, buf, sizeof(buf), 0);
 sprintf(buf, "<P>Your browser sent a bad request, ");
 send(client, buf, sizeof(buf), 0);
 sprintf(buf, "such as a POST without a Content-Length.\r\n");
 send(client, buf, sizeof(buf), 0);
}

/**********************************************************************/
/* Put the entire contents of a file out on a socket.  This function
 * is named after the UNIX "cat" command, because it might have been
 * easier just to do something like pipe, fork, and exec("cat").
 * Parameters: the client socket descriptor
 *             FILE pointer for the file to cat */
/**********************************************************************/
void cat(int client, FILE *resource)
{
 char buf[1024];
//发送文件内容
 fgets(buf, sizeof(buf), resource); //读取文件到buf中
 while (!feof(resource)) //判断文件是否读取到末尾
 {
   //读取并发送文件内容
  send(client, buf, strlen(buf), 0);
  fgets(buf, sizeof(buf), resource);
 }
}

/**********************************************************************/
/* Inform the client that a CGI script could not be executed.
 * Parameter: the client socket descriptor. */
/**********************************************************************/
void cannot_execute(int client)
{
 char buf[1024];
//发送500
 sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "Content-type: text/html\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
 send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Print out an error message with perror() (for system errors; based
 * on value of errno, which indicates system call errors) and exit the
 * program indicating an error. */
/**********************************************************************/
void error_die(const char *sc)
{
 perror(sc);
 exit(1);
}

/**********************************************************************/
/* Execute a CGI script.  Will need to set environment variables as
 * appropriate.
 * Parameters: client socket descriptor
 *             path to the CGI script */
/**********************************************************************/
//执行CGI动态解析
void execute_cgi(int client, const char *path,
                 const char *method, const char *query_string)
{
 char buf[1024];
 int cgi_output[2]; //声明读写管道
 int cgi_input[2];
 pid_t pid;
 int status;
 int i;
 char c;
 int numchars = 1;
 int content_length = -1;

 buf[0] = 'A'; buf[1] = '\0';
 if (strcasecmp(method, "GET") == 0)
 //如果是GET请求
 //读取并且丢弃头信息
  while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
   numchars = get_line(client, buf, sizeof(buf));
 else    /* POST */
 {
  //POST请求，则需要对请求头信息进行处理
  numchars = get_line(client, buf, sizeof(buf));
  while ((numchars > 0) && strcmp("\n", buf))
  {
   buf[15] = '\0';
   //循环读取，找到'Content-Length'
   if (strcasecmp(buf, "Content-Length:") == 0)
    content_length = atoi(&(buf[16])); //获取Content-Length的值
   numchars = get_line(client, buf, sizeof(buf));
  }
  if (content_length == -1) {
    //错误请求
   bad_request(client);
   return;
  }
 }

 sprintf(buf, "HTTP/1.0 200 OK\r\n"); //返回正确的响应码
 send(client, buf, strlen(buf), 0);

//#include<unistd.h>
//函数定义： int pipe(int fileders[2])
//返回值：成功返回0，否则返回-1，参数数组包括pipe使用的两个文件描述符。fd[0]:读管道，fd[1]:写管道
//必须在fork中调用pipe(),否则子进程不会继承文件描述符
//两个进程不共享祖先进程，就不能用pipe。但是可以使用命名管道
//pipe(cgi_output)执行成功后，cgi_output[0]:读通道 cgi_output[1]:写通道，这就是说不能被名称所迷惑，output包括读写通道
//申请两个匿名管道
 if (pipe(cgi_output) < 0) {
  cannot_execute(client);
  return;
 }
 if (pipe(cgi_input) < 0) {
  cannot_execute(client);
  return;
 }

//创建进程 fork进程
 if ( (pid = fork()) < 0 ) {
  cannot_execute(client);
  return;
 }
 //fork出子进程用以执行cgi脚本
 if (pid == 0)  /* child: CGI script */
 {
  char meth_env[255];
  char query_env[255];
  char length_env[255];

  dup2(cgi_output[1], 1); //1代表者stdout,0代表者stdin，将系统的标准输出重定向为cgi_output[1]
  dup2(cgi_input[0], 0); //将系统的标准输入重定向为cgi_input[0],这一点非常关键，cgi中用的是标准输入输出进行交互
  close(cgi_output[0]);
  close(cgi_input[1]);
  //CGI标准需要将请求的方法存储到环境变量中，然后和CGI脚本交互
  //存储REQUEST_METHOD
  sprintf(meth_env, "REQUEST_METHOD=%s", method);
  putenv(meth_env);
  if (strcasecmp(method, "GET") == 0) {
    //GET请求存储QUERY_STRING
   sprintf(query_env, "QUERY_STRING=%s", query_string);
   putenv(query_env);
  }
  else {   /* POST */
  //POST请求存储CONTENT_LENGTH
   sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
   putenv(length_env);
  }
  //头文件 #include <unistd.h>
  //函数的定义： int execl(const char * path,const char * arg,...)
  //函数说明：execl() 是用来执行参数path字符串所代表的文件路径，接下来的参数代表执行文件时传递过去的argv[0]、argv[1]...,最后一个参数必须用空指针（NULL）作为结束
  //返回值：如果执行成功则函数不会返回，执行失败返回-1，失败原因存储于errno中
  execl(path, path, NULL); //执行cgi脚本
  exit(0);
 } else {    /* parent */ //父进程
  close(cgi_output[1]); //关闭cgi_output[1]中的写通道，注意这是父进程的cgi_output变量，需要和子进程分开
  close(cgi_input[0]); //关闭了cgi_input中的读通道
  if (strcasecmp(method, "POST") == 0)
   for (i = 0; i < content_length; i++) {
     //开始读取post中的内容
    recv(client, &c, 1, 0);
    //将数据发送给cgi脚本
    write(cgi_input[1], &c, 1);
   }
   //读取cgi脚本返回的数据
  while (read(cgi_output[0], &c, 1) > 0)
  //发送给浏览器
   send(client, &c, 1, 0);
//运行结束，关闭通道
  close(cgi_output[0]);
  close(cgi_input[1]);
  //函数定义： pid_t waitpid(pid_t pid,int * status,int options);
  //函数说明：waitpid()会暂时停止目前进程执行，直到有信号进来，或者子进程结束
  //如果在调用wait()时子进程已经结束，则wait()会立即返回子进程结束状态值，子进程的结束状态指由status进行返回
  //而子进程的进程识别码也会被一并返回
  //如果不在意结束状态值，则参数status的值可以设置成NULL，参数pid为欲等待的子进程的识别码，其它数值意义如下：
  //1. pid<-1 等待进程组识别码为pid绝对值的任何子进程
  //2. pid=-1 等待任务子进程，相当于wait().
  //3. pid=0 等待进程组识别码与目前进程相同的如何子进程
  //4. pid>0 等待任何子进程识别码为pid的子进程
  waitpid(pid, &status, 0);
 }
}

/**********************************************************************/
/* Get a line from a socket, whether the line ends in a newline,
 * carriage return, or a CRLF combination.  Terminates the string read
 * with a null character.  If no newline indicator is found before the
 * end of the buffer, the string is terminated with a null.  If any of
 * the above three line terminators is read, the last character of the
 * string will be a linefeed and the string will be terminated with a
 * null character.
 * Parameters: the socket descriptor
 *             the buffer to save the data in
 *             the size of the buffer
 * Returns: the number of bytes stored (excluding null) */
/**********************************************************************/

//从缓冲区读取一行数据
int get_line(int sock, char *buf, int size)
{
 int i = 0;
 char c = '\0';
 int n;

 while ((i < size - 1) && (c != '\n'))
 {
  n = recv(sock, &c, 1, 0); //从sock中读取一个字符
  /* DEBUG printf("%02X\n", c); */
  if (n > 0)
  {
   if (c == '\r') //如果读到回车，一般紧接着就是\n
   {
    n = recv(sock, &c, 1, MSG_PEEK);
    /* DEBUG printf("%02X\n", c); */
    if ((n > 0) && (c == '\n')) //这是再读，还是\n字符，跳出循环
     recv(sock, &c, 1, 0);
    else
     c = '\n';
   }
   buf[i] = c;
   i++;
  }
  else
   c = '\n';
 }
 buf[i] = '\0';
 
 return(i); //返回读取的字符数
}

/**********************************************************************/
/* Return the informational HTTP headers about a file. */
/* Parameters: the socket to print the headers on
 *             the name of the file */
/**********************************************************************/
void headers(int client, const char *filename)
{
 char buf[1024];
 (void)filename;  /* could use filename to determine file type */
//发送HTTP header
 strcpy(buf, "HTTP/1.0 200 OK\r\n");
 send(client, buf, strlen(buf), 0);
 strcpy(buf, SERVER_STRING);
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "Content-Type: text/html\r\n");
 send(client, buf, strlen(buf), 0);
 strcpy(buf, "\r\n");
 send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Give a client a 404 not found status message. */
/**********************************************************************/
void not_found(int client)
{
 char buf[1024];
//发送404
 sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, SERVER_STRING);
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "Content-Type: text/html\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "your request because the resource specified\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "is unavailable or nonexistent.\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "</BODY></HTML>\r\n");
 send(client, buf, strlen(buf), 0);
}

/**********************************************************************/
/* Send a regular file to the client.  Use headers, and report
 * errors to client if they occur.
 * Parameters: a pointer to a file structure produced from the socket
 *              file descriptor
 *             the name of the file to serve */
/**********************************************************************/
//将请求的文件发送回浏览器
void serve_file(int client, const char *filename)
{
 FILE *resource = NULL;
 int numchars = 1;
 char buf[1024];

 buf[0] = 'A'; buf[1] = '\0';
 while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
  numchars = get_line(client, buf, sizeof(buf)); // 读取http请求头并丢弃

 resource = fopen(filename, "r"); //以读的方式打开文件
 if (resource == NULL)
 //如果文件不存在则返回404
  not_found(client);
 else
 {
  headers(client, filename); //添加http请求头
  cat(client, resource); //发送文件内容
 }
 fclose(resource);
}

/**********************************************************************/
/* This function starts the process of listening for web connections
 * on a specified port.  If the port is 0, then dynamically allocate a
 * port and modify the original port variable to reflect the actual
 * port.
 * Parameters: pointer to variable containing the port to connect on
 * Returns: the socket */
/**********************************************************************/
//启动服务端
int startup(u_short *port)
{
 int httpd = 0; //定义服务器socket描述符
 struct sockaddr_in name;

 httpd = socket(PF_INET, SOCK_STREAM, 0); //创建服务端的socket ipv4；sock_stream 建立安全tcp流的类型；0表示默认的协议
 if (httpd == -1)
  error_die("socket");
 memset(&name, 0, sizeof(name)); //初始化结构体
 name.sin_family = AF_INET; //绑定ipv4
 name.sin_port = htons(*port); //绑定端口，转化为网络字序节（大端存储的子序节）
 name.sin_addr.s_addr = htonl(INADDR_ANY); //本机任意可用的ip地址
 //绑定端口
 if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
  error_die("bind");
//动态分配一个端口
 if (*port == 0)  /* if dynamically allocating a port */
 {
  socklen_t namelen = sizeof(name);
  //获取已经绑定后的套接字信息，主要为了获取动态生成的端口号
  if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)
   error_die("getsockname");
  *port = ntohs(name.sin_port); //转化为ntohs类型，就是把网络字节序转化为本地的字节序
 }
 //监听连接
 if (listen(httpd, 5) < 0)
  error_die("listen");
 return(httpd); //把生成的socket描述符传递回main函数，也就是main函数中的server_sock
}

/**********************************************************************/
/* Inform the client that the requested web method has not been
 * implemented.
 * Parameter: the client socket */
/**********************************************************************/
void unimplemented(int client)
{
 char buf[1024];
//发送501说明没有实现当前请求的方法
 sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, SERVER_STRING);
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "Content-Type: text/html\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "</TITLE></HEAD>\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "</BODY></HTML>\r\n");
 send(client, buf, strlen(buf), 0);
}

/**********************************************************************/

int main(void)
{
 int server_sock = -1; //定义服务器socket的描述符
 u_short port = 8080; //定义服务器socket的端口，如果为0则动态分配服务器未占用的端口
 int client_sock = -1; //定义客户端socket描述符
 struct sockaddr_in client_name; //定义sockaddr_in 结构体，用于存储 ip、端口等信息
 socklen_t client_name_len = sizeof(client_name); //获取结构体占用空间大小
 pthread_t newthread;
//启动socker服务
 server_sock = startup(&port);
 printf("httpd running on port %d\n", port); //打印端口

 while (1) //循环监听请求，等待客户端连接
 {
   //如果有客户端请求过来，就从listen()里建立的队列里面获取一个连接，然后生成socket描述符，使用client_name进行存储
  client_sock = accept(server_sock,
                       (struct sockaddr *)&client_name,
                       &client_name_len);
  if (client_sock == -1)
   error_die("accept");
  printf("client port %d \n",ntohs(client_name.sin_port)); //打印客户端端口
  // accept_request(client_sock); 
  //启动子线程处理
  //&newthread 线程id，NULL默认参数，accept_request 子线程要执行的函数
 if (pthread_create(&newthread , NULL, accept_request,(void*)&client_sock) != 0)
   perror("pthread_create");
 }

 close(server_sock);

 return(0);
}
