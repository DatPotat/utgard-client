#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "pacbridge.h"

typedef struct { unsigned short port; unsigned char answer; volatile LONG hits; } dns_server;
static DWORD WINAPI serve(void *arg)
{
    dns_server *s=(dns_server *)arg;SOCKET fd=socket(AF_INET,SOCK_DGRAM,0);struct sockaddr_in local,peer;int plen=sizeof peer;
    unsigned char q[512],r[544];int n;DWORD timeout=8000;memset(&local,0,sizeof local);local.sin_family=AF_INET;local.sin_addr.s_addr=htonl(INADDR_LOOPBACK);local.sin_port=htons(s->port);
    setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,(char *)&timeout,sizeof timeout);if(bind(fd,(struct sockaddr *)&local,sizeof local))return 1;
    n=recvfrom(fd,(char *)q,sizeof q,0,(struct sockaddr *)&peer,&plen);if(n>=12&&n+16<=(int)sizeof r){memcpy(r,q,n);r[2]|=0x80;r[3]|=0x80;r[6]=0;r[7]=1;
        r[n]=0xc0;r[n+1]=0x0c;r[n+2]=0;r[n+3]=1;r[n+4]=0;r[n+5]=1;r[n+6]=r[n+7]=r[n+8]=0;r[n+9]=60;r[n+10]=0;r[n+11]=4;r[n+12]=203;r[n+13]=0;r[n+14]=113;r[n+15]=s->answer;
        sendto(fd,(char *)r,n+16,0,(struct sockaddr *)&peer,plen);InterlockedIncrement(&s->hits);}closesocket(fd);return 0;
}
static int query(unsigned short port,const char *name,unsigned char expected)
{
    unsigned char q[512]={0x12,0x34,1,0,0,1},r[544];size_t at=12;const char *p=name,*dot;SOCKET fd;struct sockaddr_in to;DWORD timeout=5000;int got;
    while(*p){dot=strchr(p,'.');{size_t n=dot?(size_t)(dot-p):strlen(p);if(!n||n>63)return 0;q[at++]=(unsigned char)n;memcpy(q+at,p,n);at+=n;}if(!dot)break;p=dot+1;}
    q[at++]=0;q[at++]=0;q[at++]=1;q[at++]=0;q[at++]=1;fd=socket(AF_INET,SOCK_DGRAM,0);memset(&to,0,sizeof to);to.sin_family=AF_INET;to.sin_addr.s_addr=htonl(INADDR_LOOPBACK);to.sin_port=htons(port);
    setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,(char *)&timeout,sizeof timeout);sendto(fd,(char *)q,(int)at,0,(struct sockaddr *)&to,sizeof to);got=recv(fd,(char *)r,sizeof r,0);closesocket(fd);return got>=16&&r[got-1]==expected;
}
int main(int argc,char **argv)
{
    genconf_input in={0};wchar_t err[256];const char script[]="function FindProxyForURL(u,h){return h=='vpn.example'?'PROXY ignored:9':'DIRECT';}";
    pac_script *pac;dns_server vpn={39211,11,0},sys={39212,22,0};HANDLE threads[2];FILE *f;STARTUPINFOA si={0};PROCESS_INFORMATION pi={0};char cmd[2048];WSADATA wsa;int ok;
    if(argc!=2||WSAStartup(MAKEWORD(2,2),&wsa))return 2;
    if(!pacbridge_prepare(&in,1,0,err,256))return 3;
    pac=pac_open(script,sizeof script-1,err,256);if(!pac)return 4;pacbridge_activate(&pac,1);
    threads[0]=CreateThread(NULL,0,serve,&vpn,0,NULL);threads[1]=CreateThread(NULL,0,serve,&sys,0,NULL);Sleep(100);
    f=fopen("pac-dns-route.json","wb");if(!f)return 5;
    fprintf(f,"{\"log\":{\"disabled\":true},\"inbounds\":[{\"type\":\"direct\",\"tag\":\"vpn-in\",\"listen\":\"127.0.0.1\",\"listen_port\":%d,\"network\":[\"tcp\",\"udp\"]},{\"type\":\"direct\",\"tag\":\"sys-in\",\"listen\":\"127.0.0.1\",\"listen_port\":%d,\"network\":[\"tcp\",\"udp\"]}],\"dns\":{\"servers\":[{\"type\":\"udp\",\"tag\":\"vpn\",\"server\":\"127.0.0.1\",\"server_port\":%u},{\"type\":\"udp\",\"tag\":\"sys\",\"server\":\"127.0.0.1\",\"server_port\":%u},{\"type\":\"udp\",\"tag\":\"helper\",\"server\":\"127.0.0.1\",\"server_port\":%d}],\"rules\":[{\"inbound\":[\"vpn-in\"],\"server\":\"vpn\"},{\"inbound\":[\"sys-in\"],\"server\":\"sys\"}],\"final\":\"helper\"},\"route\":{\"default_domain_resolver\":\"sys\",\"rules\":[{\"inbound\":[\"vpn-in\",\"sys-in\"],\"action\":\"hijack-dns\"}]}}",in.pac_dns_vpn_port,in.pac_dns_sys_port,vpn.port,sys.port,in.pac_dns_port);fclose(f);
    snprintf(cmd,sizeof cmd,"\"%s\" run -c pac-dns-route.json",argv[1]);si.cb=sizeof si;if(!CreateProcessA(NULL,cmd,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi))return 6;CloseHandle(pi.hThread);Sleep(600);
    ok=query((unsigned short)in.pac_dns_port,"vpn.example",11)&&query((unsigned short)in.pac_dns_port,"direct.example",22);
    TerminateProcess(pi.hProcess,0);WaitForSingleObject(pi.hProcess,3000);CloseHandle(pi.hProcess);WaitForMultipleObjects(2,threads,TRUE,9000);
    printf("PAC DNS first query=%d vpn=%ld sys=%ld\n",ok,vpn.hits,sys.hits);CloseHandle(threads[0]);CloseHandle(threads[1]);pacbridge_activate(NULL,0);remove("pac-dns-route.json");return ok&&vpn.hits==1&&sys.hits==1?0:1;
}
