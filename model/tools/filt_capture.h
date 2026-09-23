// Shared reader for little-endian CAP1 captures and signed PCM16 inputs.
#pragma once
#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>
#include <stdexcept>
struct Capture { unsigned ns,n,first; std::vector<int32_t> v; };
static Capture cap(const std::string &p) {
    FILE *f=fopen((p+".hdr").c_str(),"rb");
    if(!f)throw std::runtime_error(p);
    uint32_t h[11];
    bool valid=fread(h,4,11,f)==11 && h[0]==0x31504143 && h[1]>0 && h[1]<=4 && h[4]==0;
    fclose(f);
    if(!valid)throw std::runtime_error("capture header/errors: "+p);
    Capture c{h[1],h[2],h[3],{}}; c.v.resize(size_t(c.ns)*c.n);
    f=fopen((p+".bin").c_str(),"rb");
    if(!f)throw std::runtime_error("capture data: "+p);
    valid=fread(c.v.data(),4,c.v.size(),f)==c.v.size(); fclose(f);
    if(!valid)throw std::runtime_error("short capture: "+p);
    return c;
}
static std::vector<int16_t> input(const std::string &p) {
    FILE *f=fopen(p.c_str(),"rb"); if(!f)throw std::runtime_error(p);
    fseek(f,0,SEEK_END); long n=ftell(f); rewind(f);
    if(n<0 || n%2){fclose(f);throw std::runtime_error("invalid PCM16 input: "+p);}
    std::vector<int16_t> v(n/2);
    bool valid=fread(v.data(),2,v.size(),f)==v.size(); fclose(f);
    if(!valid)throw std::runtime_error("short PCM16 input: "+p);
    return v;
}
struct CycConfig { int F,Q,A,on; };
static const CycConfig cyc_config[]={
 {0x1ffe,4,1000,139},{0x1ffe,4,-1000,139},{0x1ffe,4,7,139},{0x1ffe,0,1000,139},
 {0x1ff0,4,1000,138},{0x1ff0,4,-1000,138},{0x1ff0,0,1000,138},{0x1ffc,4,1000,138},
 {0x1e00,4,1000,138},{0x1e00,4,-1000,138},{0x1e00,0,1000,138},{0x1e00,0,-1000,138},
 {0x1c00,4,1000,138},{0x1c00,4,-1000,138},{0x1c00,0,1000,138},{0x1c00,0,-1000,138},
 {0x1a00,4,1000,138},{0x1a00,4,-1000,138},{0x1800,4,1000,138},{0x1800,4,-1000,138},
 {0x1f55,4,1000,200},{0x1f55,4,-1000,200},{0x1d55,4,1000,200},{0x1d55,4,-1000,200}
};
