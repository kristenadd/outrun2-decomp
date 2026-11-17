// shrink_dds IN.dds OUT.dds WIDTH HEIGHT
//
// Box-filters an uncompressed 32-bit DDS (any byte order, e.g. the
// OR2006Sprites A8B8G8R8 files) down to WIDTH x HEIGHT and writes it as
// A8R8G8B8. The size must divide evenly. Same filter as the game's loader
// (src/platform/button_prompts.cpp), so the result looks identical; it only
// saves disk space (the pack's sheets are 4x the game's size).
// Exit 2: the input is not uncompressed 32-bit (copy it as it is instead).
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t* p){uint32_t v;memcpy(&v,p,4);return v;}
static void wr32(uint8_t* p,uint32_t v){memcpy(p,&v,4);}
static int mask_byte(uint32_t m){for(int k=0;k<4;++k)if(m==(0xffu<<(8*k)))return k;return -1;}

int main(int argc,char** argv){
    if(argc!=5){fprintf(stderr,"usage: %s IN.dds OUT.dds WIDTH HEIGHT\n",argv[0]);return 1;}
    const uint32_t w=(uint32_t)strtoul(argv[3],0,10),h=(uint32_t)strtoul(argv[4],0,10);
    FILE* f=fopen(argv[1],"rb");if(!f){perror(argv[1]);return 1;}
    fseek(f,0,SEEK_END);long size=ftell(f);fseek(f,0,SEEK_SET);
    if(size<128){fprintf(stderr,"%s: too small\n",argv[1]);return 1;}
    uint8_t* in=malloc((size_t)size);
    if(!in||fread(in,1,(size_t)size,f)!=(size_t)size){fprintf(stderr,"%s: read failed\n",argv[1]);return 1;}
    fclose(f);
    if(memcmp(in,"DDS ",4)){fprintf(stderr,"%s: not a DDS file\n",argv[1]);return 1;}
    const uint32_t sh=rd32(in+12),sw=rd32(in+16),pf=rd32(in+80),bits=rd32(in+88);
    if((pf&4u)||bits!=32u)return 2;
    const int ch[4]={mask_byte(rd32(in+92)),mask_byte(rd32(in+96)),mask_byte(rd32(in+100)),(pf&1u)?mask_byte(rd32(in+104)):-1};
    if(ch[0]<0||ch[1]<0||ch[2]<0)return 2;
    if(!w||!h||sw<w||sh<h||sw%w||sh%h){fprintf(stderr,"%s: %ux%u is not a multiple of %ux%u\n",argv[1],sw,sh,w,h);return 1;}
    if((size_t)size<128+(size_t)sw*sh*4){fprintf(stderr,"%s: shorter than its image\n",argv[1]);return 1;}
    const size_t outsize=128+(size_t)w*h*4;
    uint8_t* out=calloc(1,outsize);if(!out)return 1;
    memcpy(out,"DDS ",4);wr32(out+4,124);wr32(out+8,0x100fu);wr32(out+12,h);wr32(out+16,w);wr32(out+20,w*4);wr32(out+28,1);
    wr32(out+76,32);wr32(out+80,0x41u);wr32(out+88,32);wr32(out+92,0xff0000u);wr32(out+96,0xff00u);wr32(out+100,0xffu);wr32(out+104,0xff000000u);wr32(out+108,0x1000u);
    const uint32_t rx=sw/w,ry=sh/h,n=rx*ry;
    const uint8_t* src=in+128;uint8_t* px=out+128;
    for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){
        uint64_t r=0,g=0,b=0,a=0;
        for(uint32_t dy=0;dy<ry;++dy){
            const uint8_t* s=src+((size_t)(y*ry+dy)*sw+(size_t)x*rx)*4;
            for(uint32_t dx=0;dx<rx;++dx,s+=4){
                const unsigned al=ch[3]<0?255u:s[ch[3]];
                r+=(uint64_t)s[ch[0]]*al;g+=(uint64_t)s[ch[1]]*al;b+=(uint64_t)s[ch[2]]*al;a+=al;}
        }
        uint8_t* d=px+((size_t)y*w+x)*4;
        if(a){d[2]=(uint8_t)((r+a/2)/a);d[1]=(uint8_t)((g+a/2)/a);d[0]=(uint8_t)((b+a/2)/a);}
        d[3]=(uint8_t)((a+n/2)/n);
    }
    f=fopen(argv[2],"wb");
    if(!f||fwrite(out,1,outsize,f)!=outsize||fclose(f)){perror(argv[2]);return 1;}
    return 0;
}
