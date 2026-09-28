#include "genconf.h"
#include "defconfig.h"
#include <stdio.h>
#include <string.h>
static int write_base(void)
{
    FILE *f = fopen("build/config.default.json", "wb");
    if (!f) return 0;
    fwrite(utgard_default_config, 1, utgard_default_config_len, f);
    fclose(f);
    return 1;
}

int main(int argc, char **argv)
{
    profile_store store;genconf_input input;char *text=NULL,error[256];FILE *file;
    if(argc!=2||!write_base())return 2;
    memset(&store,0,sizeof store);
    memset(&input,0,sizeof input);
    store.count=1;store.active=0;store.items[0].link.proto=LINK_SS;strcpy(store.items[0].link.name,"compare");
    strcpy(store.items[0].link.server,"203.0.113.10");store.items[0].link.port=443;strcpy(store.items[0].link.method,"aes-128-gcm");strcpy(store.items[0].link.password,"test-only-password");
    input.base_path="build/config.default.json";input.rule_set_path="build/general.srs";input.store=&store;
    if(!genconf_build(&input,&text,error,sizeof error)){fprintf(stderr,"%s\n",error);return 3;}
    file=fopen(argv[1],"wb");if(!file){genconf_text_free(text);return 4;}fwrite(text,1,strlen(text),file);fclose(file);genconf_text_free(text);return 0;
}
