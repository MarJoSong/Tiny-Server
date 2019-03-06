#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <dirent.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "epoll_server.h"

#define MAXSIZE 2000

void epoll_run(int port)
{
    //创建epoll树的根节点
    int epfd = epoll_create(MAXSIZE);
    if(epfd==-1)
    {
        perror("epoll_create error");
        exit(1);
    }

    //先添加监听节点
    int lfd = init_listen_fd(port, epfd);

    //委托内核检测添加到树上的节点
    struct epoll_event all[MAXSIZE];
    while(1)
    {
        int len = epoll_wait(epfd, all, MAXSIZE, -1);
        if(len==-1)
        {
            perror("epoll_wait error");
            exit(1);
        }

        for(int i=0;i<len;++i)
        {
            //只处理读事件，其他事件不处理
            struct epoll_event* pev = &all[i];
            if(!(pev->events & EPOLLIN))
            {
                //不是读事件
                continue;
            }
            if(pev->data.fd==lfd)
            {
                //接收连接请求
                do_accept(lfd, epfd);
            }
            else
            {
                //读数据
                do_read(pev->data.fd, epfd);
            }
        }

    }
}

int init_listen_fd(int port, int epfd)
{
    //创建监听套接字
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if(lfd==-1)
    {
        perror("socket error");
        exit(1);
    }

    //lfd绑定本地IP和端口
    struct sockaddr_in serv;
    memset(&serv, 0x00, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_port = htons(port);
    serv.sin_addr.s_addr = htonl(INADDR_ANY);
    //设置端口复用
    int flag = 1;//要设置flag=1
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    int ret = bind(lfd, (struct sockaddr*)&serv, sizeof(serv));
    if(ret==-1)
    {
        perror("bind error");
        exit(1);
    }

    //设置监听
    ret = listen(lfd, 64);
    if(ret==-1)
    {
        perror("listen error");
        exit(1);
    }

    //将监听节点添加到epoll树上
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = lfd;
    ret = epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev);
    if(ret==-1)
    {
        perror("epoll_ctl_add  lfd errror");
        exit(1);
    }

    return lfd;
}

void do_accept(int lfd, int epfd)
{
    //接收连接请求
    struct sockaddr_in client;
    socklen_t socklen = sizeof(client);
    int cfd = accept(lfd, (struct sockaddr*)&client, &socklen);
    if(cfd==-1)
    {
        perror("accept error");
        exit(1);
    }

    //打印客户端信息
    char ip[64] = {0};
    printf("New Client: %s , Port: %d, Cfd: %d\n",
           inet_ntop(AF_INET, &client.sin_addr.s_addr, ip, sizeof(ip)),
           ntohs(client.sin_port), cfd);

    //设置cfd为非阻塞
    int flags = fcntl(cfd, F_GETFL);
    flags |= O_NONBLOCK;
    fcntl(cfd, F_SETFL, flags);

    //添加cfd到epoll树上
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;  //设置边沿触发
    ev.data.fd = cfd;
    int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev);
    if(ret==-1)
    {
        perror("epoll_ctl_add cfd error");
        exit(1);
    }
}

void do_read(int cfd, int epfd)
{
    //读请求行
    char line[1024];
    int len = get_line(cfd, line, sizeof(line));
    if(len==0)
    {
        printf("The Client has disconnected\n");
        disconnect(cfd, epfd);
    }
    else
    {
        printf("The Request line data: %s", line);
        //读请求头
        printf("===============Request Head===============\n");
        while(len)
        {
            char buf[1024];
            len = get_line(cfd, buf, sizeof(buf));
            printf("--:%s", buf);
        }
        printf("===============Request HEnd===============\n");
    }

    //判断请求是否为'GET'
    if(strncasecmp(line, "GET", 3)==0)
    {
        //处理http请求
        http_request(line, cfd);
        //将cfd从epoll树上摘下来
        disconnect(cfd, epfd);
    }
}

void disconnect(int cfd, int epfd)
{
    //摘掉没有用的节点
    int ret = epoll_ctl(epfd, EPOLL_CTL_DEL, cfd, NULL);
    if(ret==-1)
    {
        perror("epoll_ctl_del cfd error");
        exit(1);
    }
    //关闭套接字文件描述符
    close(cfd);
}

int get_line(int sock, char *buf, int size)
{
    int i = 0;
    char c = '\0';
    int n;
    while((i<size-1)&&(c!='\n'))
    {
        n = recv(sock, &c, 1, 0);
        if(n>0)
        {
            if(c=='\r')
            {
                n = recv(sock, &c, 1, MSG_PEEK);
                if((n>0)&&(c=='\n'))
                {
                    recv(sock, &c, 1, 0);
                }
                else
                {
                    c = '\n';
                }
            }
            buf[i] = c;
            ++i;
        }
        else
        {
            c = '\n';
        }
    } 
    buf[i] = '\0';
    return i;
}

void http_request(const char* request, int cfd)
{
    //拆分请求头
    char method[12], path[1024], protocol[12];
    sscanf(request, "%[^ ] %[^ ] %[^ ]", method, path, protocol);

    printf("method = %s, path = %s, protocol = %s\n", method, path, protocol);
    // 转码 将不能识别的中文乱码 - > 中文   
    // 解码 %23 %34 %5f
    decode_str(path, path);
    //处理path  /xx
    // 去掉path中的/
    char* file = path+1;
    // 如果没有指定访问的资源, 默认显示资源目录中的内容
    if(strcmp(path, "/")==0)
    {
        file = "./";
    }

    struct stat st;
    int ret = stat(file, &st);
    if(ret==-1)
    {
        //文件不存在,404
        send_respond_head(cfd, 404, "File Not Found", ".html", -1);
        send_file(cfd, "404.html");
    }

    //判断是目录还是文件
    //目录
    if(S_ISDIR(st.st_mode))
    {
        //发送文件头
        send_respond_head(cfd, 200, "OK", get_file_type(".html"), -1);
        //发送目录
        send_dir(cfd, file);
    }
    //文件
    if(S_ISREG(st.st_mode))
    {
        //发送文件头
        send_respond_head(cfd, 200, "OK", get_file_type(file), st.st_size);
        //发送文件
        send_file(cfd, file);
    }
}

void send_respond_head(int cfd, int no, const char* desp, const char* type, long len)
{
    char buf[1024] = {0};
    //状态行
    sprintf(buf, "HTTP/1.1 %d %s\r\n", no, desp);
    send(cfd, buf, strlen(buf), 0);
    //消息报头
    sprintf(buf, "Content-Type:%s\r\n", type);
    sprintf(buf+strlen(buf), "Content-Length:%ld\r\n", len);
    send(cfd, buf, strlen(buf), 0);
    //空行
    send(cfd, "\r\n", 2, 0);
}

void send_file(int cfd, const char* filename)
{
    //打开文件
    int fd = open(filename, O_RDONLY);
    if(fd==-1)
    {
        //show 404
        return;
    }

    char buf[4096] = {0};
    int len = 0;
    //循环读发文件
    while((len = read(fd, buf, sizeof(buf)))>0)
    {
        send(cfd, buf, len, 0);
    }
    if(len==-1)
    {
        perror("read file error");
        exit(1);
    }
    //关闭文件描述符
    close(fd);
}

void send_dir(int cfd, const char* dirname)
{
    //拼接一个html页面
    char buf[4096] = {0};
    sprintf(buf, "<html><head><title>目录名: %s</title></head>", dirname);
    sprintf(buf+strlen(buf), "<body><h1>当前目录: %s</h1><table>", dirname);

    char enstr[1024] = {0}; //编码后文件名
    char path[1024] = {0};  //完整路径
    struct dirent** ptr;    //目录项二级指针
    int num = scandir(dirname, &ptr, NULL, alphasort);
    //遍历
    for(int i=0;i<num;++i)
    {
        char* name = ptr[i]->d_name;

        //拼接完整的文件名
        sprintf(path, "%s/%s", dirname, name);
        printf("path:  %s=================\n", path);

        //判断是文件还是目录
        struct stat st;
        stat(path, &st);
        //文件
        encode_str(enstr, sizeof(enstr), name);
        if(S_ISREG(st.st_mode))
        {
            sprintf(buf+strlen(buf), "<tr><td><a href=\"%s\">%s</a></td><td>%ld</td></tr>",enstr, name, (long)st.st_size);
        }
        //目录
        else if(S_ISDIR(st.st_mode))
        {
            sprintf(buf+strlen(buf), "<tr><td><a href=\"%s/\">%s</a></td><td>%ld</td></tr>",enstr, name, (long)st.st_size);
        }
        send(cfd, buf, strlen(buf), 0);
        memset(buf, 0x00, sizeof(buf));
    }
    //xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
    sprintf(buf, "</table></body></html>");
    send(cfd, buf, strlen(buf), 0);
    printf("dir mseeage send OK!\n");
}

const char *get_file_type(const char *name)
{
    char* dot;

    // 自右向左查找‘.’字符, 如不存在返回NULL
    dot = strrchr(name, '.');
    if(dot==NULL)
        return "text/plain; charset=utf-8";
    if (strcmp(dot, ".html")==0 || strcmp(dot, ".htm")==0)
        return "text/html; charset=utf-8";
    if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
        return "image/jpeg";
    if (strcmp(dot, ".gif") == 0)
        return "image/gif";
    if (strcmp(dot, ".png") == 0)
        return "image/png";
    if (strcmp(dot, ".css") == 0)
        return "text/css";
    if (strcmp(dot, ".au") == 0)
        return "audio/basic";
    if (strcmp( dot, ".wav"  ) == 0)
        return "audio/wav";
    if (strcmp(dot, ".avi") == 0)
        return "video/x-msvideo";
    if (strcmp(dot, ".mov") == 0 || strcmp(dot, ".qt") == 0)
        return "video/quicktime";
    if (strcmp(dot, ".mpeg") == 0 || strcmp(dot, ".mpe") == 0)
        return "video/mpeg";
    if (strcmp(dot, ".vrml") == 0 || strcmp(dot, ".wrl") == 0)
        return "model/vrml";
    if (strcmp(dot, ".midi") == 0 || strcmp(dot, ".mid") == 0)
        return "audio/midi";
    if (strcmp(dot, ".mp3") == 0)
        return "audio/mpeg";
    if (strcmp(dot, ".ogg") == 0)
        return "application/ogg";
    if (strcmp(dot, ".pac") == 0)
        return "application/x-ns-proxy-autoconfig";

    return "text/plain; charset=utf-8";
}

/*
 * 这里的内容是处理%20之类的东西！是"解码"过程。
 * %20 URL编码中的‘ ’(space)
 * %21 '!' %22 '"' %23 '#' %24 '$'
 * %25 '%' %26 '&' %27 ''' %28  '''('......
 * 相关知识html中的‘ ’(space)是&nbsp
 */
void encode_str(char* to, int tosize, const char* from)
{
    int tolen;
    for (tolen = 0; *from != '\0' && tolen + 4 < tosize; ++from) 
    {
        if (isalnum(*from) || strchr("/_.-~", *from) != (char*)0) 
        {
            *to = *from;
            ++to;
            ++tolen;
        } 
        else 
        {
            sprintf(to, "%%%02x", (int) *from & 0xff);
            to += 3;
            tolen += 3;
        }
    }
    *to = '\0';
}

void decode_str(char *to, char *from)
{
    for ( ; *from != '\0'; ++to, ++from   ) 
    {
        if (from[0] == '%' && isxdigit(from[1]) && isxdigit(from[2])) 
        { 
            *to = hexit(from[1])*16 + hexit(from[2]);
            from += 2;                      
        } 
        else
        {
            *to = *from;
        }
    }
    *to = '\0';
}

int hexit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;

    return 0;
}
