#include "pacblob.h"
#include <string.h>

typedef struct { uint32_t h[8]; uint64_t bits; unsigned char block[64]; size_t used; } sha256_ctx;
static uint32_t rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }
static void sha_block(sha256_ctx *c, const unsigned char *p)
{
    static const uint32_t k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
    uint32_t w[64], a,b,d,e,f,g,h,i,t1,t2,cc; unsigned j;
    for (j=0;j<16;j++) w[j]=((uint32_t)p[j*4]<<24)|((uint32_t)p[j*4+1]<<16)|((uint32_t)p[j*4+2]<<8)|p[j*4+3];
    for (;j<64;j++) { uint32_t x=w[j-15], y=w[j-2]; w[j]=(rotr(x,7)^rotr(x,18)^(x>>3))+w[j-16]+(rotr(y,17)^rotr(y,19)^(y>>10))+w[j-7]; }
    a=c->h[0]; b=c->h[1]; cc=c->h[2]; d=c->h[3]; e=c->h[4]; f=c->h[5]; g=c->h[6]; h=c->h[7];
    for (i=0;i<64;i++) { t1=h+(rotr(e,6)^rotr(e,11)^rotr(e,25))+((e&f)^((~e)&g))+k[i]+w[i]; t2=(rotr(a,2)^rotr(a,13)^rotr(a,22))+((a&b)^(a&cc)^(b&cc)); h=g;g=f;f=e;e=d+t1;d=cc;cc=b;b=a;a=t1+t2; }
    c->h[0]+=a;c->h[1]+=b;c->h[2]+=cc;c->h[3]+=d;c->h[4]+=e;c->h[5]+=f;c->h[6]+=g;c->h[7]+=h;
}
static void sha_init(sha256_ctx *c) { static const uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}; memset(c,0,sizeof *c); memcpy(c->h,h,sizeof h); }
static void sha_update(sha256_ctx *c, const void *data, size_t n)
{
    const unsigned char *p=(const unsigned char *)data; c->bits+=(uint64_t)n*8;
    while(n){size_t take=64-c->used;if(take>n)take=n;memcpy(c->block+c->used,p,take);c->used+=take;p+=take;n-=take;if(c->used==64){sha_block(c,c->block);c->used=0;}}
}
static void sha_final(sha256_ctx *c, unsigned char out[32])
{
    uint64_t bits=c->bits; unsigned i;c->block[c->used++]=0x80;
    if(c->used>56){memset(c->block+c->used,0,64-c->used);sha_block(c,c->block);c->used=0;}
    memset(c->block+c->used,0,56-c->used);for(i=0;i<8;i++)c->block[63-i]=(unsigned char)(bits>>(i*8));sha_block(c,c->block);
    for(i=0;i<8;i++){out[i*4]=(unsigned char)(c->h[i]>>24);out[i*4+1]=(unsigned char)(c->h[i]>>16);out[i*4+2]=(unsigned char)(c->h[i]>>8);out[i*4+3]=(unsigned char)c->h[i];}memset(c,0,sizeof *c);
}
static void hash(const char *script,size_t n,unsigned char out[32]){sha256_ctx c;sha_init(&c);sha_update(&c,script,n);sha_final(&c,out);}

size_t pacblob_pack(const char *script, size_t n, const uint16_t *source, size_t units, unsigned char *out, size_t cap)
{
    size_t need; unsigned char digest[32];
    if (!script || !n || !source || units < 2 || units > PAC_BLOB_SOURCE_MAX ||
        units > (SIZE_MAX-33)/2 || !out || source[units-1]) return 0;
    need = 33 + units * 2;
    if (cap < need) return 0;
    out[0]=PAC_BLOB_FORMAT;hash(script,n,digest);memcpy(out+1,digest,32);memcpy(out+33,source,units*2);memset(digest,0,sizeof digest);return need;
}
int pacblob_unpack(const unsigned char *blob,size_t length,const char *script,size_t n,uint16_t *source,size_t cap,size_t *units)
{
    unsigned char digest[32];size_t count;
    if(!blob||length<35||blob[0]!=PAC_BLOB_FORMAT||((length-33)&1)||!script||!n)return 0;
    count=(length-33)/2;if(count<2||count>PAC_BLOB_SOURCE_MAX||count>cap||!source)return 0;hash(script,n,digest);
    if(memcmp(digest,blob+1,32)){memset(digest,0,sizeof digest);return 0;}memset(digest,0,sizeof digest);
    memcpy(source,blob+33,count*2);if(source[count-1])return 0;{size_t i;for(i=0;i+1<count;i++)if(!source[i])return 0;}if(units)*units=count;return 1;
}

size_t pacitem_pack(int enabled, const uint16_t *source, size_t units,
                    const char *script, size_t n, unsigned char *out, size_t cap)
{
    size_t need = 1 + 1 + 2 + units * 2 + 4 + n, i, at = 0;
    if (!source || !script || !out || !units || units > PAC_BLOB_SOURCE_MAX || source[units - 1] != 0 ||
        !n || n > 0xFFFFFFFFu || need > cap) return 0;
    out[at++] = (unsigned char)PAC_ITEM_FORMAT;
    out[at++] = enabled ? 1 : 0;
    out[at++] = (unsigned char)(units & 0xFF); out[at++] = (unsigned char)(units >> 8);
    for (i = 0; i < units; i++) { out[at++] = (unsigned char)(source[i] & 0xFF); out[at++] = (unsigned char)(source[i] >> 8); }
    for (i = 0; i < 4; i++) out[at++] = (unsigned char)((uint32_t)n >> (8 * i));
    memcpy(out + at, script, n);
    return need;
}

int pacitem_unpack(const unsigned char *b, size_t len, size_t script_max,
                   int *enabled, uint16_t *source, size_t source_cap, size_t *source_units,
                   const unsigned char **script, size_t *script_length)
{
    size_t units, n, i, at = 4;
    if (!b || len < 1 + 1 + 2 + 2 + 4 + 1 || b[0] != PAC_ITEM_FORMAT || b[1] > 1) return 0;
    units = (size_t)b[2] | ((size_t)b[3] << 8);
    if (!units || units > PAC_BLOB_SOURCE_MAX || units > source_cap || len < at + units * 2 + 4) return 0;
    for (i = 0; i < units; i++) source[i] = (uint16_t)(b[at + 2 * i] | (b[at + 2 * i + 1] << 8));
    if (source[units - 1] != 0) return 0;
    at += units * 2;
    n = (size_t)b[at] | ((size_t)b[at + 1] << 8) | ((size_t)b[at + 2] << 16) | ((size_t)b[at + 3] << 24);
    at += 4;
    if (!n || n > script_max || len != at + n) return 0;
    *enabled = b[1];
    if (source_units) *source_units = units;
    *script = b + at;
    *script_length = n;
    return 1;
}
