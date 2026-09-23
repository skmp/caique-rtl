// Exact integer width search for endpoint-resonance captures. Initial state and
// Q-write sample are nuisance parameters, fixed once per complete trajectory.
#include "filt_capture.h"
#include <algorithm>
#include <cstdlib>
#include <climits>
#ifdef VERIFY_MODEL
#include "../src/aica_model.h"
#endif
using I=int64_t;
struct R{int match=-1,b0=0,event=0;I maxL=0,maxB=0;};
static I limit(I v,int w,int mode){
 if(!mode)return v;I lo=-(I(1)<<(w-1)),hi=-lo-1;
 if(mode==1)return std::clamp(v,lo,hi);
 v&=(I(1)<<w)-1;return v>hi?v-(I(1)<<w):v;
}
static int stage=0;
static R run(const Capture&c,int slot,int F,int on,int ev,int width,int mode){
 R best;int k=F==0x1ffe?512:256+((F>>1)&255);
 for(int b=-16;b<=16;b++)for(int event=ev-64;event<=ev+64;event++){
  I L=-I(c.v[(on-1)*4+slot])/2,B=b,maxL=std::abs(L),maxB=std::abs(B);int n=on;
  for(;n<(int)c.n;n++){
   I x=c.v[n*4+3]/2,q=n<event?192:13;
   I D=2*(-((-q*B)>>8));
   if(stage==2)D=limit(D,width,mode);
   I H=x-L-D;if(stage==1)H=limit(H,width,mode);
   I inc=(k*H)>>9;if(stage==3)inc=limit(inc,width,mode);
   B+=inc;if(stage==0)B=limit(B,width,mode);
   I li=-((-k*B)>>9);if(stage==4)li=limit(li,width,mode);
   L+=li;if(stage==0)L=limit(L,width,mode);
   maxL=std::max(maxL,std::abs(L));maxB=std::max(maxB,std::abs(B));
   if(std::clamp<I>(-2*L,-524288,524287)!=c.v[n*4+slot])break;
  }
  if(n-on>best.match)best={n-on,b,event,maxL,maxB};
#ifdef VERIFY_MODEL
  if(n==(int)c.n){
   caique::AicaModel m;m.write(0x28,64);m.write(0x20,240);
   auto &s=m.slot[0];s.enabled=true;s.AEG.off=true;s.update_rate=0;s.FEG.v=F;
   s.lpf_low=-I(c.v[(on-1)*4+slot])/2;s.lpf_band=b;
   for(int t=on;t<(int)c.n;t++){
    if(t==event)m.write(0x28,64|31);
    s.s0=s.s1=c.v[t*4+3]/16;m.step();
    if(m.MIXS[0]!=c.v[t*4+slot]){
     fprintf(stderr,"PRODUCTION FAIL slot=%d F=%04x n=%d expected=%d actual=%d\n",slot,F,t,c.v[t*4+slot],m.MIXS[0]);exit(2);
    }
   }
  }
#endif

  if(n==(int)c.n)return best;
  // No Q-write time can repair a mismatch before the first possible write.
  if(n<ev-64)break;
 }
 return best;
}
int main(int argc,char**argv){
 if(argc>1)stage=atoi(argv[1]);
 long long samples=0;int full=0,total=0;
 const bool heldout=argc>3;
 const int fs[3]={heldout?0x1ffa:0x1ffe,heldout?0x1ff6:0x1ffc,heldout?0x1ff0:0x1ff8};
 for(int batch=0;batch<(heldout?4:5);batch++){
  std::string p=std::string("tests/")+(heldout?"filt_overflow_check":"filt_overflow")+"/hw/fo_"+std::to_string(batch);auto c=cap(p);int on=1;
  while(on<(int)c.n&&!c.v[on*4+3])on++;
  FILE*f=fopen((p+".hdr").c_str(),"rb");uint32_t h[64]={};size_t nh=fread(h,4,64,f);fclose(f);int ev=-1;
  for(size_t i=11;i+1<nh;i+=2)if(h[i]==2)ev=int(h[i+1]-c.first);
  if(ev<0)return 1;
  printf("batch %d on=%d Q_write_mark=%d N=%u\n",batch,on,ev,c.n);
  for(int slot=0;slot<3;slot++){
   for(int mode=0;mode<3;mode++)for(int w=20;w<=(mode?32:20);w++){
    if(argc>2 && (mode!=1 || w!=atoi(argv[2])))continue;
    auto r=run(c,slot,fs[slot],on,ev,w,mode);
    total++;if(r.match==int(c.n-on)){full++;samples+=r.match;}
    printf(" s%d F=%04x mode=%s W=%d %d/%u b0=%d event=%d maxL=%lld maxB=%lld%s\n",slot,fs[slot],mode==0?"unbounded":mode==1?"clamp":"wrap",w,r.match,c.n-on,r.b0,r.event,(long long)r.maxL,(long long)r.maxB,r.match==int(c.n-on)?" FULL":"");
   }
  }
 }
 printf("TOTAL full=%d/%d matched_full_samples=%lld\n",full,total,samples);
 return argc>2 && full!=total;
}
