#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>

static int io(SOCKET s, unsigned char *p, int n, int writing)
{ while(n){int k=writing?send(s,(char *)p,n,0):recv(s,(char *)p,n,0);if(k<=0)return 0;p+=k;n-=k;}return 1; }
static int address(SOCKET s, unsigned char *atyp)
{
    unsigned char n; int rest;
    if(!io(s,atyp,1,0))return 0;
    if(*atyp==1)rest=6;else if(*atyp==4)rest=18;else if(*atyp==3){if(!io(s,&n,1,0))return 0;rest=n+2;}else return 0;
    while(rest){unsigned char b[256];int take=rest>256?256:rest;if(!io(s,b,take,0))return 0;rest-=take;}return 1;
}
int main(int argc,char **argv)
{
    WSADATA w;SOCKET l,c,u=INVALID_SOCKET;struct sockaddr_in a;int size=sizeof a;unsigned char b[2048],atyp=0,command;int n;
    if(argc!=2||WSAStartup(MAKEWORD(2,2),&w))return 2;
    l=socket(AF_INET,SOCK_STREAM,0);memset(&a,0,sizeof a);a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons((unsigned short)atoi(argv[1]));
    if(bind(l,(struct sockaddr *)&a,sizeof a)||listen(l,1))return 3;
    c=accept(l,NULL,NULL);if(c==INVALID_SOCKET)return 4;
    puts("accepted");fflush(stdout);
    if(!io(c,b,2,0)||b[0]!=5||!io(c,b,b[1],0))return 5;b[0]=5;b[1]=0;if(!io(c,b,2,1))return 5;
    if(!io(c,b,3,0)||b[0]!=5||!address(c,&atyp))return 6;
    printf("request command=%u ATYP=%u\n",(unsigned)b[1],(unsigned)atyp);fflush(stdout);
    command=b[1];if(command==3){
        u=socket(AF_INET,SOCK_DGRAM,0);memset(&a,0,sizeof a);a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        if(bind(u,(struct sockaddr *)&a,sizeof a)||getsockname(u,(struct sockaddr *)&a,&size))return 7;
        {unsigned char reply[10]={5,0,0,1,127,0,0,1,0,0};memcpy(reply+8,&a.sin_port,2);if(!io(c,reply,10,1))return 7;}
        n=recv(u,(char *)b,sizeof b,0);if(n<4)return 8;atyp=b[3];
    } else { unsigned char reply[10]={5,0,0,1,127,0,0,1,0,0};if(!io(c,reply,10,1))return 9;recv(c,(char *)b,sizeof b,0); }
    printf("SOCKS command=%u ATYP=%u\n",(unsigned)command,(unsigned)atyp);fflush(stdout);
    if(u!=INVALID_SOCKET)closesocket(u);closesocket(c);closesocket(l);return 0;
}
