#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>

#define N 4096


//日志文件

void writelog(const char *mylog)
{
    time_t tDate;
    //结构体保存的是年月日 时分秒
    struct tm *eventTime;

    //获取从 1970年1月1日  到现在有多少秒
    time(&tDate);
    //返回结构体  结构体里存储系统时间
    eventTime=localtime(&tDate);

    //tm_year 返回的是从1900年开始的 加上1900
    int iYear=eventTime->tm_year+1900;
    //tm_mon 返回的是0-11
    int iMon=eventTime->tm_mon+1;
    int iDay=eventTime->tm_mday;
    int iHour=eventTime->tm_hour;
    int iMin=eventTime->tm_min;
    int iSec=eventTime->tm_sec;

    //年月日
    char sDate[16]={0};
    sprintf(sDate,"%04d-%02d-%02d",iYear,iMon,iDay);
    //时分秒
    char sTime[16]={0};
    sprintf(sTime,"%02d:%02d:%02d",iHour,iMin,iSec);

    char text[N]={0};
    sprintf(text,"%s %s %s\n",sDate,sTime,mylog);

    const char *slogfile="my.log";
    FILE *fd=fopen(slogfile,"a+");
    if(fd==NULL)
    {
        printf("open %s failed %s\n",slogfile,strerror(errno));
    }
    else
    {
        fputs(text,fd);
        fclose(fd);
    }
}

static int hexit(char c); 
/*
 * 这里的内容是处理%20之类的东西！是"解码"过程。
 * %20 URL编码中的‘ ’(space)
 * %21 '!' %22 '"' %23 '#' %24 '$'
 * %25 '%' %26 '&' %27 ''' %28 '('......
 * 相关知识html中的‘ ’(space)是&nbsp
 */
static void strdecode(char *to, char *from)
{
    for ( ; *from != '\0'; ++to, ++from) {
	
		if (from[0] == '%' && isxdigit(from[1]) && isxdigit(from[2])) { //依次判断from中 %20 三个字符
	    
	    	*to = hexit(from[1])*16 + hexit(from[2]);
	    	from += 2;                      //移过已经处理的两个字符(%21指针指向1),表达式3的++from还会再向后移一个字符
	    } else
	    	*to = *from;
	}
    *to = '\0';
}

//16进制数转化为10进制, return 0不会出现 
static int hexit(char c)
{
    if (c >= '0' && c <= '9')
		return c - '0';
    if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;

    return 0;		
}

//"编码"，用作回写浏览器的时候，将除字母数字及/_.-~以外的字符转义后回写。
//strencode(encoded_name, sizeof(encoded_name), name);
static void strencode(char* to, size_t tosize, const char* from)
{
    size_t tolen;

    for (tolen = 0; (*from!='\0')&&(tolen+4<tosize); ++from) {
		if (isalnum(*from) || strchr("/_.-~", *from) != (char*)0) {
			*to = *from;
			++to;
			++tolen;
		} else {
			sprintf(to, "%%%02x", (int) *from & 0xff);	
			to += 3;
			tolen += 3;
		}
	}
    *to = '\0';

}
void send_headers(char *type)
{
    //发送回复的http协议   每行都要\r\n
    printf("HTTP/1.1 %d Ok\r\n",200);
    //回复文件的类型
    printf("Content-Type:%s\r\n",type);
    //发送完 断开连接
    printf("Connection:close\r\n");
    //结尾一定要\r\n
    printf("\r\n");
}

void send_error(int status,char *title,char *text)
{
    //发送协议头   网页
    //html的用/n换行即可
    send_headers("text/html");
    printf("<html><head><title>%d %s</title></head>\n",status,title);
    printf("<body bgcolor=\"#cc99cc\"><h4>%d %s</h4>\n",status,title);
    printf("%s\n",text);
    printf("<hr>\n</body>\n</html>\n");
    //刷新标准输出
    fflush(stdout);
    //信息发送完即关闭
    exit(1);
}

//发送网页
//发送网页 参数 1 标题  2 GET 后面获取的路径 例：/aa 3 文件名  4文件个数
void send_html(char *title,char *path,char *text[],int count)
{
    int i=0;
    printf("<html><head><title>%s</title></head>\n",title);
    //背景色
    printf("<body bgcolor=\"#cc99cc\"><h4>%s</h4>\n",title);
    for(i=0;i<count;++i)
    {
        char send_path[N]={0};
        //此处为  /xxxx/xxx   path 为GET /xxx/xxx HTTP/1.1 获取的文件路径
        strcat(send_path,path);
        //此处为必须  因为当光输入例：192.1.1.1:2222  path会自动变为/ 其情况末尾没有/
        if(path[strlen(path)-1]!='/')
        {
            strcat(send_path,"/");
        }
        strcat(send_path,text[i]);
        char encoded_name[N]={0};
        strencode(encoded_name,sizeof(encoded_name),send_path);
        printf("<a href=\"%s\">%s</a><br/>",encoded_name,text[i]);
    }
    printf("<hr>\n</body>\n</html>\n");
    fflush(stdout);
    exit(1);
}

//读取目录下的所有文件  并以网页的形式展示
//第一个参数 访问的目录的绝对路径  第二个参数 /xxx/xxx
void read_dir(char *all_path,char *path)
{
    int i=0;
    char *readpath=all_path;

    struct dirent **namelist;
    //读取当前目录下所有文件和目录的名字，1 目录名  2 二维数组存放  3 屏蔽的问价或目录  4 排序 按字母或时间排序
    int ret=scandir(readpath,&namelist,NULL,alphasort);
    if(ret==-1)
    {
        send_error(111,"not find","not fount");
    }
    char *temp[ret];
    for(i=0;i<ret;i++)
    {
        temp[i]=namelist[i]->d_name;
    }
    send_headers("text/html");
    send_html("haha",path,temp,ret);
}
int main(int argc,char *argv[])
{
    //读到的数据
    char line[N];
    //方法：GET    路径：     协议：HTTP/1.1
    char method[N],path[N],protocol[N];
    char *file;
    struct stat sbuf;
    FILE *fp;
    int ich;
    char *type=NULL;

    if(argc!=2)
    {
        send_error(500, "Internal Error", "Config error - no dir specified.");
    }
    if(chdir(argv[1])==-1)
    {
        send_error(501, "Internal Error", "Config error - couldn't chdir().");
    }
    if(fgets(line,sizeof(line),stdin)==NULL)
    {
        send_error(400, "Bad Request", "No request found.");
    }
    //正则表达式
    if(sscanf(line,"%[^ ] %[^ ] %[^ ]",method,path,protocol)!=3)
    {
        send_error(401, "Bad Request", "Can't parse request.");
    }
    //循环读完所有发来的数据
    while(fgets(line,sizeof(line),stdin)!=NULL)
    {
        if(strcmp(line,"\r\n")==0 || strcmp(line,"\n")==0)
        {
            break;
        }
    } 
    if(strcmp(method,"GET")!=0)
    {
        send_error(501, "Not Implemented", "That method is not implemented.");
    }
    if(path[0]!='/')
    {
        send_error(400, "Bad Request", "Bad filename.");
    }
    //file 为去除‘/’的路径
    file=path+1;

    //解码  将%ed  之类转化成汉字
    strdecode(file,file);

    //说明是根目录
    if(strcmp("",file)==0)
    {
        file="./";
    }

    //stat 这个函数第一个参数  是基于当前工作路径下的文件名
    if(lstat(file,&sbuf)<0)
    {
        send_error(404,argv[1],path);
    }
    if(S_ISDIR(sbuf.st_mode))
    {
        char str[N]={0};
        strcat(str,argv[1]);
        //path 前面 有/
        strcat(str,path);
        read_dir(str,path);
    }
    fp=fopen(file,"r");
    if(fp==NULL)
    {
        send_error(403, "Forbidden", "File is protected.");
    }
    //strrchr 获取最后一个ch
    char *dot = strrchr(file, '.');

    if(dot == NULL){
        type = "text/plain; charset=utf-8";
    } else if(strcmp(dot, ".html") == 0){
        type = "text/html; charset=utf-8";
    } else if(strcmp(dot, ".jpg") == 0){
        type = "image/jpeg";
    } else if(strcmp(dot, ".gif") == 0){
        type = "image/gif";
    } else if(strcmp(dot, ".png") == 0){
        type = "image/png";
    } else if(strcmp(dot, ".mp3") == 0){
        type = "audio/mpeg";
    }  else {
        type = "text/plain; charset=iso-8859-1";
    }
    send_headers(type);
    while((ich=getc(fp))!=EOF)
    {
        putchar(ich);
    }
    fflush(stdout);
    fclose(fp);

    return 0;
}

