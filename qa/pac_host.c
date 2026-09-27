#include "pacblob.h"
#include "pacguard.h"
#include "paclogic.h"
#include "pacrecord.h"
#include "pacudp.h"
#include <assert.h>
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
    values[0] = -1; assert(paclogic_any(1, by_index, NULL, &failed) == 0 && failed);
    r = paclogic_route(1, "vpn.example", names, 0, by_name, NULL);
    assert(r.vpn && r.rewrite && !strcmp(r.rewrite_host, "vpn.example"));
    strcpy(names[0], "vpn.example"); r = paclogic_route(0, "198.51.100.1", names, 1, by_name, NULL);
    assert(r.vpn && r.rewrite && !strcmp(r.rewrite_host, "vpn.example"));
    strcpy(names[1], "direct.example"); r = paclogic_route(0, "198.51.100.1", names, 2, by_name, NULL);
    assert(r.vpn && !r.rewrite);
    r = paclogic_route(0, "198.51.100.9", names, 0, by_name, NULL); assert(r.vpn && !r.rewrite);
}

static void test_udp(void)
{
    unsigned char packet[512] = {0,0,0,1,1,2,3,4,0,53,'x'};
    unsigned char v4[] = {1,5,6,7,8,1,187};
    unsigned char v6[19] = {4}; unsigned char domain[] = {3,3,'a','b','c',0,80}; size_t n;
    v6[18]=53;
    n=pacudp_restore(packet,11,sizeof packet,domain,sizeof domain);assert(n==11&&packet[3]==3&&packet[10]=='x');
    n=pacudp_restore(packet,n,sizeof packet,v6,sizeof v6);assert(n==23&&packet[3]==4&&packet[22]=='x');
    n=pacudp_restore(packet,n,sizeof packet,v4,sizeof v4);assert(n==11&&packet[3]==1&&packet[10]=='x');
    assert(!pacudp_restore(packet,2,sizeof packet,v4,sizeof v4));
    assert(!pacudp_restore(packet,11,2,v4,sizeof v4));
    packet[0]=1;assert(!pacudp_restore(packet,11,sizeof packet,v4,sizeof v4));
    {
        size_t cap=65536,payload=cap-3-sizeof domain; unsigned char *large=(unsigned char *)calloc(1,cap);
        assert(large);memcpy(large+3,domain,sizeof domain);large[3+sizeof domain+payload-1]=0x5a;
        assert(pacudp_restore(large,cap,cap,v4,sizeof v4)==cap-sizeof domain+sizeof v4);
        assert(large[cap-sizeof domain+sizeof v4-1]==0x5a);free(large);
    }
}

static void test_guard(void)
{
    char entries[2][256]={{0}};assert(pacguard_enter(entries,2,"Example.COM"));
    assert(!pacguard_enter(entries,2,"example.com"));pacguard_leave(entries,2,"EXAMPLE.com");
    assert(pacguard_enter(entries,2,"example.com"));
}

static void test_record(void)
{
    pac_record a={0},b={0};a.magic=b.magic=PAC_STATUS_MAGIC;a.version=b.version=PAC_STATUS_VERSION;
    a.sequence_begin=a.sequence_end=b.sequence_begin=b.sequence_end=2;assert(pacrecord_consistent(&a,&b));
    b.sequence_end=3;assert(!pacrecord_consistent(&a,&b));b.sequence_end=2;b.sequence_begin=4;assert(!pacrecord_consistent(&a,&b));
}

static void test_blob(void)
{
    const char script[]="function FindProxyForURL(){return 'DIRECT';}";uint16_t source[]={ 'h','t','t','p','s',':','/','/','x',0 },out_source[32];
    unsigned char blob[256],copy[256];size_t n,units=0;
    n=pacblob_pack(script,strlen(script),source,sizeof source/2,blob,sizeof blob);assert(n);
    {static const unsigned char abc[32]={0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};unsigned char known[128];size_t k=pacblob_pack("abc",3,source,sizeof source/2,known,sizeof known);assert(k&&memcmp(known+1,abc,32)==0);}
    assert(pacblob_unpack(blob,n,script,strlen(script),out_source,32,&units)&&units==sizeof source/2&&!memcmp(source,out_source,sizeof source));
    assert(!pacblob_unpack(blob,n,"tampered",8,out_source,32,NULL));memcpy(copy,blob,n);copy[0]=9;
    assert(!pacblob_unpack(copy,n,script,strlen(script),out_source,32,NULL));assert(!pacblob_unpack(blob,n-1,script,strlen(script),out_source,32,NULL));
    assert(!pacblob_pack(script,strlen(script),source,sizeof source/2,blob,10));
    assert(!pacblob_pack(script,strlen(script),source,PAC_BLOB_SOURCE_MAX+1u,blob,sizeof blob));
    assert(pacblob_store_version_supported(1)&&pacblob_store_version_supported(2)&&pacblob_store_version_supported(3)&&!pacblob_store_version_supported(4));
}

int main(void)
{ test_logic();test_udp();test_guard();test_record();test_blob();puts("PAC host tests: OK");return 0; }
