#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>
#include <time.h>

#define MAX_EVENTS 1024   //监听的上限
#define BUFLEN 4096       //缓冲区大小
#define SERV_PORT 2222   //服务器端口

void recvdata(int fd,int events,void *arg);
void acceptconn(int fd,int events,void *arg);

struct myevent_s
{
    //要监听的文件描述符
    int fd;
    //要监听的事件类型
    int events;
    //指向自己的指针
    void *arg;
    //回调函数，保存着需要执行的操作（例如：读或写或监听处理）
    void(*call_back)(int fd,int events,void *arg);
    //状态 1 表示在 树上   0表示不在树上
    int status;
    //数据
    char buf[BUFLEN];
    //保存数据的长度
    int len;
    //保存上一次活跃的时间
    long last_active;
};

int efd;//保存全部变量红黑树的根节点
struct myevent_s g_events[MAX_EVENTS+1];//保存所有发生过变化的文件描述符，最后一个保存监听的文件描述符

//初始化  myevent_s 成员
void eventset(struct myevent_s * ev,int fd,void(*call_back)(int ,int,void *),void *arg)
{
    //初始化文件描述符
    ev->fd=fd;
    ev->arg=arg;
    ev->call_back=call_back;
    //初始化 表示不在树上
    ev->status=0;
    //保存调用的时间
    ev->last_active=time(NULL);
    ev->events=0;
    //    memset(ev->buf,0,BUFLEN);
    //    ev->len=0;

    return ;
}

//将要监听的文件描述符加到树上
void eventadd(int efd,int events,struct myevent_s *ev)
{
    struct epoll_event epv;
    memset(&epv,0,sizeof(epv));
    //保存加载到树上时的状态
    int op;
    //保存myevent_s 结构体
    epv.data.ptr=ev;
    //将要监听的事件 加载到要放在树上的结构体里
    epv.events=ev->events=events;

    //如果已经在红黑树上，那么就修改其属性，否则加载到树上
    if(ev->status==1)
    {
        op=EPOLL_CTL_MOD;

    }
    else if(ev->status==0)
    {
        op=EPOLL_CTL_ADD;
        //状态置1
        ev->status=1;
    }

    if((epoll_ctl(efd,op,ev->fd,&epv))<0)
    {
        printf("epoll_ctl error\n");
    }
    else
    {
        //printf("event add successful!!!\n");
    }

    return ;
}

void eventdel(int efd,struct myevent_s *ev)
{
    struct epoll_event epv;
    memset(&epv,0,sizeof(epv));

    //如果不在红黑树上
    if(ev->status!=1)
    {
        return;
    }
    //指向myevent_s的结构体
    epv.data.ptr=ev;
    //修改标志位
    ev->status=0;
    //从树上删除
    epoll_ctl(efd,EPOLL_CTL_DEL,ev->fd,&epv);

    return;
}
//创建socket 初始化lfd
void initlistensocket(int efd,short port)
{
    int lfd=socket(AF_INET,SOCK_STREAM,0);
    //将监听的文件描述符设置为非阻塞
    fcntl(lfd,F_SETFL,O_NONBLOCK);
    //初始化监听的结构体  把lfd等相关信息存储在myevent_s的结构体中，lfd的执行时间是接受连接
    eventset(&g_events[MAX_EVENTS],lfd,acceptconn,&g_events[MAX_EVENTS]);
    //将监听的lfd加载到树上
    eventadd(efd,EPOLLIN,&g_events[MAX_EVENTS]);

    struct sockaddr_in sin;
    memset(&sin,0,sizeof(sin));
    sin.sin_family=AF_INET;
    sin.sin_port=htons(SERV_PORT);
    sin.sin_addr.s_addr=htonl(INADDR_ANY);

    bind(lfd,(struct sockaddr*)&sin,sizeof(sin));

    listen(lfd,64);
    return ;
}

void acceptconn(int fd,int events,void *arg)
{
    struct sockaddr_in cin;
    socklen_t len=sizeof(cin);
    //通信用的文件描述符
    int cfd,i;

    if((cfd=accept(fd,(struct sockaddr*)&cin,&len))==-1)
    {
        //如果不是收到信号中断或者断开连接  其他出错
        if((errno!=EAGAIN)&&(errno!=EINTR))
        {
            perror("accept  error!!\n");
        }
        return ;
    }

    //此处加while(0)的作用是为了用break
    do
    {
        //找到一个空闲的位置  在全局数组 g_events中
        for(i=0;i<MAX_EVENTS;i++)   
        {
            if(g_events[i].status==0)
            {
                break;
            }
        }

        if(i==MAX_EVENTS)
        {
            printf("max connect!!\n");
            break;
        }

        int flag=0;
        if((flag=fcntl(cfd,F_SETFL,O_NONBLOCK))<0)
        {
            printf("fcntl set noblock fail!!\n");
            break;
        }

        //将通信的文件描述符设置为接收状态
        eventset(&g_events[i],cfd,recvdata,&g_events[i]);
        eventadd(efd,EPOLLIN,&g_events[i]);
    }while(0);

    char ipbuf[1024]={0};
    printf("new connet IP:%s  ,  port:%d\n",inet_ntop(AF_INET,&(cin.sin_addr.s_addr),ipbuf,sizeof(ipbuf)),ntohs(cin.sin_port));
    return ;
}
void recvdata(int fd,int events,void *arg)
{
    struct myevent_s *ev=(struct myevent_s *)arg;

    pid_t pid=fork();
    if(pid==0)
    {
        dup2(fd,STDIN_FILENO);
        dup2(fd,STDOUT_FILENO);
        close(fd);
        execl("/home/itheima/web_service/myhttpd","myhttpd","/home/itheima/mydir",NULL);
    }
    if(pid>0)
    {
        signal(SIGCHLD,SIG_IGN);
        eventdel(efd,ev);
        close(fd);
    }
    return ; 
}

int main(int argc,char *argv[])
{
    unsigned short port=SERV_PORT;
    if(argc==2)
    {
        port=atoi(argv[1]);
    }
    efd=epoll_create(MAX_EVENTS+1);
    if(efd<=0)
    {
        perror("epoll_create error!!!\n");
        exit(1);
    }
    //初始化监听的文件描述符  ，并将其挂载树上
    initlistensocket(efd,port);

    struct epoll_event events[MAX_EVENTS+1];
    printf("servers runing!!!\n");

    //每次检查100个 checkpos累加到最大值的时候清0 
    int checkpos=0,i;
    while(1)
    {
        //超时验证，每次检查100个连接，不测试listen的文件描述符，其他60秒不连接就断开
        long now=time(NULL);
        for(i=0;i<100;i++,checkpos++)
        {
            if(checkpos==MAX_EVENTS)
            {
                checkpos=0;
            }
            if(g_events[checkpos].status!=1)
            {
                continue;
            }
            long duration=now-g_events[checkpos].last_active;

            if(duration>=60)
            {
                close(g_events[checkpos].fd);
                printf("%d timeout\n",g_events[checkpos].fd);
                eventdel(efd,&g_events[checkpos]);
            }
        }

        //监听 如果1秒没有事件满足，返回0
        int nfd=epoll_wait(efd,events,MAX_EVENTS+1,1000);
        if(nfd<0)
        {
            perror("wait error!!\n");
            exit(1);
        }

        for(int i=0;i<nfd;i++)
        {
            struct myevent_s *ev=(struct myevent_s*)events[i].data.ptr;

            if((events[i].events & EPOLLIN)&&(ev->events&EPOLLIN))
            {
                ev->call_back(ev->fd,events[i].events,ev->arg);
            }
        }

    }



    return 0;
}








