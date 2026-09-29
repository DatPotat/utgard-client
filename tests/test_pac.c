#include "pacblob.h"
#include "pacguard.h"
#include "paclogic.h"
#include "pacrecord.h"
#include "pacudp.h"
#include "pacloop.h"
#include "check.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int values[8];
static int by_index(size_t i, void *unused) { (void)unused; return values[i]; }
static int by_name(const char *name, int domain, void *unused)
{ (void)domain; (void)unused; return !strcmp(name, "vpn.example") || !strcmp(name, "198.51.100.9"); }

static void test_logic(void)
{
    int failed = 0; char names[2][256] = {{0}}; paclogic_result r;
    values[0] = -1; CHECK(paclogic_any(1, by_index, NULL, &failed) == 0 && failed);
    r = paclogic_route(1, "vpn.example", names, 0, by_name, NULL);
    CHECK(r.vpn && r.rewrite && !strcmp(r.rewrite_host, "vpn.example"));
    strcpy(names[0], "vpn.example"); r = paclogic_route(0, "198.51.100.1", names, 1, by_name, NULL);
    CHECK(r.vpn && r.rewrite && !strcmp(r.rewrite_host, "vpn.example"));
    strcpy(names[1], "direct.example"); r = paclogic_route(0, "198.51.100.1", names, 2, by_name, NULL);
    CHECK(r.vpn && !r.rewrite);
    r = paclogic_route(0, "198.51.100.9", names, 0, by_name, NULL); CHECK(r.vpn && !r.rewrite);
}

static void test_udp(void)
{
    unsigned char packet[512] = {0,0,0,1,1,2,3,4,0,53,'x'};
    unsigned char v4[] = {1,5,6,7,8,1,187};
    unsigned char v6[19] = {4}; unsigned char domain[] = {3,3,'a','b','c',0,80}; size_t n;
    v6[18]=53;
    n=pacudp_restore(packet,11,sizeof packet,domain,sizeof domain);CHECK(n==11&&packet[3]==3&&packet[10]=='x');
    n=pacudp_restore(packet,n,sizeof packet,v6,sizeof v6);CHECK(n==23&&packet[3]==4&&packet[22]=='x');
    n=pacudp_restore(packet,n,sizeof packet,v4,sizeof v4);CHECK(n==11&&packet[3]==1&&packet[10]=='x');
    CHECK(!pacudp_restore(packet,2,sizeof packet,v4,sizeof v4));
    CHECK(!pacudp_restore(packet,11,2,v4,sizeof v4));
    packet[0]=1;CHECK(!pacudp_restore(packet,11,sizeof packet,v4,sizeof v4));
    {
        size_t cap=65536,payload=cap-3-sizeof domain; unsigned char *large=(unsigned char *)calloc(1,cap);
        CHECK(large);memcpy(large+3,domain,sizeof domain);large[3+sizeof domain+payload-1]=0x5a;
        CHECK(pacudp_restore(large,cap,cap,v4,sizeof v4)==cap-sizeof domain+sizeof v4);
        CHECK(large[cap-sizeof domain+sizeof v4-1]==0x5a);free(large);
    }
}

static void test_guard(void)
{
    char entries[2][256]={{0}};CHECK(pacguard_enter(entries,2,"Example.COM"));
    CHECK(!pacguard_enter(entries,2,"example.com"));pacguard_leave(entries,2,"EXAMPLE.com");
    CHECK(pacguard_enter(entries,2,"example.com"));
}

static void test_record(void)
{
    pac_record a={0},b={0};a.magic=b.magic=PAC_STATUS_MAGIC;a.version=b.version=PAC_STATUS_VERSION;
    a.sequence_begin=a.sequence_end=b.sequence_begin=b.sequence_end=2;CHECK(pacrecord_consistent(&a,&b));
    b.sequence_end=3;CHECK(!pacrecord_consistent(&a,&b));b.sequence_end=2;b.sequence_begin=4;CHECK(!pacrecord_consistent(&a,&b));
}

static void test_blob(void)
{
    const char script[]="function FindProxyForURL(){return 'DIRECT';}";uint16_t source[]={ 'h','t','t','p','s',':','/','/','x',0 },out_source[32];
    unsigned char blob[256],copy[256];size_t n,units=0;
    n=pacblob_pack(script,strlen(script),source,sizeof source/2,blob,sizeof blob);CHECK(n);
    {static const unsigned char abc[32]={0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};unsigned char known[128];size_t k=pacblob_pack("abc",3,source,sizeof source/2,known,sizeof known);CHECK(k&&memcmp(known+1,abc,32)==0);}
    CHECK(pacblob_unpack(blob,n,script,strlen(script),out_source,32,&units)&&units==sizeof source/2&&!memcmp(source,out_source,sizeof source));
    CHECK(!pacblob_unpack(blob,n,"tampered",8,out_source,32,NULL));
    {   /* one PAC per file: round trip, and every malformed shape refused */
        static const uint16_t src[] = { 'h','t','t','p','s',':','/','/','a',0 };
        unsigned char item[256], bad[256]; const unsigned char *sc; size_t sl, u, m; int en; uint16_t back[16];
        m = pacitem_pack(1, src, 10, "function FindProxyForURL(){}", 28, item, sizeof item);
        CHECK(m == 1 + 1 + 2 + 20 + 4 + 28);
        CHECK(pacitem_unpack(item, m, 4096, &en, back, 16, &u, &sc, &sl) && en == 1 && u == 10 &&
              sl == 28 && !memcmp(sc, "function FindProxyForURL(){}", 28) && !memcmp(back, src, sizeof src));
        CHECK(!pacitem_unpack(item, m - 1, 4096, &en, back, 16, &u, &sc, &sl));       /* short */
        memcpy(bad, item, m); bad[m] = 0;
        CHECK(!pacitem_unpack(bad, m + 1, 4096, &en, back, 16, &u, &sc, &sl));        /* trailing byte */
        memcpy(bad, item, m); bad[0] = 1;
        CHECK(!pacitem_unpack(bad, m, 4096, &en, back, 16, &u, &sc, &sl));            /* wrong format */
        memcpy(bad, item, m); bad[1] = 2;
        CHECK(!pacitem_unpack(bad, m, 4096, &en, back, 16, &u, &sc, &sl));            /* bad flag */
        CHECK(!pacitem_unpack(item, m, 27, &en, back, 16, &u, &sc, &sl));             /* over the limit */
        CHECK(!pacitem_unpack(item, m, 4096, &en, back, 5, &u, &sc, &sl));            /* source too long */
        memcpy(bad, item, m); bad[4 + 18] = 'x';
        CHECK(!pacitem_unpack(bad, m, 4096, &en, back, 16, &u, &sc, &sl));            /* source not terminated */
        CHECK(!pacitem_pack(1, src, 10, "x", 1, item, 10));                           /* no room */
        CHECK(!pacitem_pack(1, src, 9, "x", 1, item, sizeof item));                   /* source without NUL */
    }memcpy(copy,blob,n);copy[0]=9;
    CHECK(!pacblob_unpack(copy,n,script,strlen(script),out_source,32,NULL));CHECK(!pacblob_unpack(blob,n-1,script,strlen(script),out_source,32,NULL));
    CHECK(!pacblob_pack(script,strlen(script),source,sizeof source/2,blob,10));
    CHECK(!pacblob_pack(script,strlen(script),source,PAC_BLOB_SOURCE_MAX+1u,blob,sizeof blob));
}

int main(void)
{
    /* select(): -1 is an error to stop on, never to loop over; 0 is a timeout. */
    CHECK(pacloop_select(-1) == PACLOOP_ERROR && pacloop_select(-100) == PACLOOP_ERROR);
    CHECK(pacloop_select(0) == PACLOOP_IDLE);
    CHECK(pacloop_select(1) == PACLOOP_READY && pacloop_select(514) == PACLOOP_READY);
    test_logic(); test_udp(); test_guard(); test_record(); test_blob();
    {   /* a full guard table refuses a new name; freeing a slot admits it */
        char full[2][256] = {{0}};
        CHECK(pacguard_enter(full, 2, "a.example") && pacguard_enter(full, 2, "b.example"));
        CHECK(!pacguard_enter(full, 2, "c.example"));
        pacguard_leave(full, 2, "a.example");
        CHECK(pacguard_enter(full, 2, "c.example"));
        CHECK(!pacguard_enter(full, 2, "") && !pacguard_enter(NULL, 2, "x.example"));
    }
    return DONE("pac");
}
