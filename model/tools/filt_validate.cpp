// Independent validation of the coarse damping SVF, using full captures and known inputs.
// Build: make -C tools filt_validate filt_validate_model (-> build/tools/; the _model variant is compiled with
// -DVERIFY_MODEL and sample-model/aica_model.cpp and also checks the real AicaModel output)
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <string>
#include <algorithm>
#include <stdexcept>
#ifdef VERIFY_MODEL
#include "aica_model.h"   /* -I../sample-model or -I../cycle-model (tools/Makefile) */
#endif
using I=int64_t;
#include "filt_capture.h"
static const int qm[32]={192,176,160,144,128,120,112,104,96,88,80,72,64,60,56,52,48,44,40,36,32,30,28,26,24,22,20,18,16,15,14,13};
static I ceildiv(I n,int s){return -((-n)>>s);}
static int qbias=255;
static void step(I&L,I&B,I x,int F,int Q){
 int k=F>=0x1ffe?512:256+((F>>1)&255),s=24-(F>>9);
 I damping=2*((qm[Q]*B+qbias)>>8);
 I high=x-L-damping;                                   // saturates to signed 24 bits (tests/filt_overflow)
 high=high<-8388608?-8388608:high>8388607?8388607:high;
 B+=(k*high)>>s;
 L+=ceildiv(k*B,s);
}
struct Result{int matched=0,total=0,band=0;I pred=0,actual=0;};
static Result check(const Capture&c,int stream,int F,int Q,const std::vector<int16_t>&in,int onset,int input_on,bool wrap=false){
 Result best;best.total=c.n-onset;
 for(int b=-256;b<=256;b++){
  I L=-I(c.v[(onset-1)*c.ns+stream])/2,B=b;int n=onset;
  for(;n<(int)c.n;n++){
   int ix=n-onset+input_on;if(wrap)ix&=65535;I x=ix>=0&&ix<(int)in.size()?I(in[ix])*8:0;
   step(L,B,x,F,Q);
   I observed=std::clamp<I>(-2*L,-524288,524287);
   if(observed!=c.v[n*c.ns+stream])break;
  }
  if(n-onset>best.matched){best.matched=n-onset;best.band=b;best.pred=L;best.actual=n<(int)c.n?-I(c.v[n*c.ns+stream])/2:L;}
  if(n==(int)c.n)break;
 }
#ifdef VERIFY_MODEL
 if(best.matched==best.total) {
  caique::AicaModel m;
  m.write(0x28,64|Q);m.write(0x20,240);m.write(0x18,0x4000);   /* OCT -8 + the phase reset below: the slot never advances */
  auto &s=m.slot[0];s.enabled=true;s.AEG.off=true;s.FEG.v=F;
  s.lpf_low=-I(c.v[(onset-1)*c.ns+stream])/2;s.lpf_band=best.band;
  for(int n=onset;n<(int)c.n;n++){
   int ix=n-onset+input_on;if(wrap)ix&=65535;
   s.s0=s.s1=ix>=0&&ix<(int)in.size()?in[ix]:0;s.step=0;
   m.step();
   if(m.MIXS[0]!=c.v[n*c.ns+stream]){
    fprintf(stderr,"PRODUCTION mismatch F=%04x Q=%d n=%d model=%d hardware=%d\n",F,Q,n,m.MIXS[0],c.v[n*c.ns+stream]);
    best.matched=n-onset;break;
   }
  }
 }
#endif

 return best;
}
int main(int argc,char**argv){
 if(argc>1)qbias=atoi(argv[1]);
 int full=0,total=0;I samples=0,matched=0;
 auto report=[&](const char*caseid,int batch,int k,int F,int Q,const Result&r){
  total++;full+=r.matched==r.total;samples+=r.total;matched+=r.matched;
  printf("%s %d:%d F=%04x Q=%d %d/%d initial_band=%d",caseid,batch,k,F,Q,r.matched,r.total,r.band);
  if(r.matched!=r.total)printf(" first predicted=%lld actual=%lld",(long long)r.pred,(long long)r.actual);
  puts("");
 };
 for(int i=0;i<24;i++){
  const auto&cf=cyc_config[i];auto c=cap("tests/filt_cyc/hw/fc_"+std::to_string(i/4));
  std::vector<int16_t> xi(256,cf.A);
  report("cyc",i/4,i%4,cf.F,cf.Q,check(c,i%4,cf.F,cf.Q,xi,cf.on,0));
 }

 const int fs[]={0x1800,0x1a00,0x1b55,0x1c00,0x1d00,0x1dab,0x1e00,0x1e01,0x1f00,0x1f80};
 const int qs[]={0,4,8,16,24,31};
 auto in=input("tests/filt_id2/hw/input.bin");
 for(int batch=0;batch<15;batch++){
  auto c=cap("tests/filt_id2/hw/fi_"+std::to_string(batch));int on=1;
  for(;on<(int)c.n;on++){bool found=false;for(unsigned k=0;k<c.ns;k++)found|=std::abs(c.v[on*c.ns+k])>100;if(found)break;}
  if(on==(int)c.n)throw std::runtime_error("missing impulse");
  for(int k=0;k<4;k++){int i=batch*4+k;report("id2",batch,k,fs[i/6],qs[i%6],check(c,k,fs[i/6],qs[i%6],in,on,64));}
 }
 std::vector<int> cf,cq;
 for(int F:{0x1c00,0x1e00})for(int Q=0;Q<32;Q++){cf.push_back(F);cq.push_back(Q);}
 for(int e=11;e<=15;e++)for(int m:{0,1,2,3,0x55,0xaa,0xff,0x100,0x155,0x1fe,0x1ff}){cf.push_back((e<<9)|m);cq.push_back(4);}
 auto ci=input("tests/filt_coef/hw/input.bin");
 for(int batch=0;batch<30;batch++){
  auto c=cap("tests/filt_coef/hw/fi_"+std::to_string(batch));int on=1;
  for(;on<(int)c.n;on++){bool found=false;for(int k=0;k<4&&batch*4+k<(int)cf.size();k++)found|=std::abs(c.v[on*c.ns+k])>100;if(found)break;}
  if(on==(int)c.n)throw std::runtime_error("missing coef impulse");
  for(int k=0;k<4&&batch*4+k<(int)cf.size();k++){int i=batch*4+k;report("coef",batch,k,cf[i],cq[i],check(c,k,cf[i],cq[i],ci,on,64));}
 }

 const int fi[]={0x1c00,0x1d55,0x1e80,0x1f55};
 for(int batch=0;batch<4;batch++){
  auto a=input("tests/filt_imp/hw/amps_"+std::to_string(batch)+".bin");std::vector<int16_t> xi(64+128*a.size()+64);
  for(size_t i=0;i<a.size();i++)xi[64+128*i]=a[i];
  auto c=cap("tests/filt_imp/hw/fm_"+std::to_string(batch));int on=1;
  for(;on<(int)c.n;on++)if(std::abs(c.v[on*c.ns])>100)break;
  for(int k=0;k<4;k++)report("imp",batch,k,fi[k],0,check(c,k,fi[k],0,xi,on,64,true));
 }
 const int edge[4][3]={{0x1ffe,0x1ffc,0x1e00},{0x1dfe,0x1dfc,0x1c00},{0x1bfe,0x1bfc,0x1a00},{0x19fe,0x19fc,0x1800}};
 for(int batch=0;batch<4;batch++){
  auto c=cap("tests/filt_edges/hw/edge_"+std::to_string(batch));std::vector<int16_t> xi(c.n);int on=1;
  for(unsigned n=0;n<c.n;n++)xi[n]=c.v[n*4+3]/16;
  while(on<(int)c.n&&!xi[on])on++;
  for(int k=0;k<3;k++)report("edges",batch,k,edge[batch][k],4,check(c,k,edge[batch][k],4,xi,on,on));
 }
 {
  auto c=cap("tests/filt_top/hw/ft");int on=1;
  while(on<(int)c.n&&std::abs(c.v[on*4+1])<100)on++;
  std::vector<int16_t> rnd(1024);uint32_t seed=3;
  for(auto &v:rnd){seed=seed*1103515245u+12345u;v=(int16_t)(seed>>16);}
  for(int k=0;k<4;k++){
   std::vector<int16_t> xi(c.n);
   for(unsigned n=on;n<c.n;n++)xi[n]=k==1?1000:k==3?rnd[(n-on)%1024]:0;
   report("top",0,k,k==3?0x1fff:0x1ffe,4,check(c,k,k==3?0x1fff:0x1ffe,4,xi,on,on));
  }
 }

 {
  // filt_low: low exponents from a known small state (unity-cutoff prelude); stream 3 = unfiltered reference
  const int lf[6][3]={{0x0000,0x0200,0x0400},{0x0600,0x0800,0x0a00},{0x0c00,0x0e00,0x1000},
                      {0x1200,0x1400,0x1600},{0x01fe,0x0955,0x13fe},{0x0a00,0x0a00,0x0400}};
  const int lq[6][3]={{4,4,4},{4,4,4},{4,4,4},{4,4,4},{4,4,4},{0,31,31}};
  for(int batch=0;batch<6;batch++){
   std::string p="tests/filt_low/hw/fl_"+std::to_string(batch);
   FILE*t=fopen((p+".hdr").c_str(),"rb");if(!t)break;fclose(t);
   auto c=cap(p);std::vector<int16_t> xi(c.n);int on=1;
   for(unsigned n=0;n<c.n;n++)xi[n]=c.v[n*4+3]/16;
   while(on<(int)c.n&&!xi[on])on++;
   for(int k=0;k<3;k++)report("low",batch,k,lf[batch][k],lq[batch][k],check(c,k,lf[batch][k],lq[batch][k],xi,on,on));
  }
 }
 {
  // filt_wide: resonant Q31 square-wave drive, states to ~2^21.7 (integrator width); stream 3 = reference
  const int wf[4]={0x1800,0x1a00,0x1600,0x1400};
  const int wq[4][3]={{31,28,24},{31,30,20},{31,29,16},{31,31,31}};
  for(int batch=0;batch<4;batch++){
   std::string p="tests/filt_wide/hw/fw_"+std::to_string(batch);
   FILE*t=fopen((p+".hdr").c_str(),"rb");if(!t)break;fclose(t);
   auto c=cap(p);std::vector<int16_t> xi(c.n);int on=1;
   for(unsigned n=0;n<c.n;n++)xi[n]=c.v[n*4+3]/16;
   while(on<(int)c.n&&!xi[on])on++;
   for(int k=0;k<3;k++)report("wide",batch,k,wf[batch],wq[batch][k],check(c,k,wf[batch],wq[batch][k],xi,on,on));
  }
 }
 printf("TOTAL qbias=%d full=%d/%d consecutive samples=%lld/%lld\n",qbias,full,total,(long long)matched,(long long)samples);
 return full!=total;
}
