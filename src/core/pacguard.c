#include "pacguard.h"
#include <string.h>

static int same(const char *a, const char *b)
{
    while (*a && *b) { unsigned char x=(unsigned char)*a++, y=(unsigned char)*b++; if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;if(x!=y)return 0; }
    return *a == *b;
}
int pacguard_enter(char (*entries)[256], int count, const char *name)
{
    int i, free_slot=-1; size_t n=name?strlen(name):0;
    if(!entries||count<=0||!n||n>=256)return 0;
    for(i=0;i<count;i++){if(entries[i][0]&&same(entries[i],name))return 0;if(free_slot<0&&!entries[i][0])free_slot=i;}
    if(free_slot<0)return 0;
    memcpy(entries[free_slot],name,n+1);
    return 1;
}
void pacguard_leave(char (*entries)[256], int count, const char *name)
{ int i;if(!entries||!name)return;for(i=0;i<count;i++)if(entries[i][0]&&same(entries[i],name)){memset(entries[i],0,256);break;} }
