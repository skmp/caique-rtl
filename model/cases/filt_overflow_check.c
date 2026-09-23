/* Unity-cutoff Q0 has a pole at -1. Alternating input grows the internal state.
 * After a finite burst, Q is changed to 31 to reveal the hidden state in decay.
 * The bypass stream records the exact input; marker 2 bounds the Q writes. */
#include "cap.h"
#define NS 4
#define MAXV (1u << 20)
#define NSIG 32768
static int32_t capbuf[MAXV];
static int16_t sig[NSIG];
static const int lengths[]={33,257,2048,4096};
static const uint16_t fv[3]={0x1ffa,0x1ff6,0x1ff0};
static void keys(int on){
 for(int k=0;k<4;k++)aw(CH(k,0),(ar(CH(k,0))&0x3fff)|(on?0x4000:0));
 aw(CH(0,0),(ar(CH(0,0))&0x7fff)|0x8000);
}
static void cfg(int k,uint32_t sa,int prelude){
 slot_cfg_t c;slot_cfg_default(&c,sa,prelude?1024:NSIG-1);
 c.LPCTL=prelude;c.ISEL=k;c.VOFF=1;c.LPOFF=k==3;c.Q=prelude?4:0;
 for(int j=0;j<5;j++)c.FLV[j]=prelude||k==3?0x1ffe:fv[k];
 c.FAR=c.FD1R=c.FD2R=c.FRR=0;slot_write(k,&c);
}
int test_main(void){
 out_open("filt_overflow_check.txt");ram_fill(0x40000,0,4096);
 for(unsigned b=0;b<sizeof lengths/sizeof lengths[0];b++){
  aica_quiet();for(int k=0;k<4;k++)cfg(k,0x40000,1);keys(1);spin_us(5000);keys(0);spin_us(2000);
  memset(sig,0,sizeof sig);
  const int amps[]={16384,-32768,8191,-12345};
  for(int i=0;i<lengths[b];i++){int a=(i&1)?-amps[b]:amps[b];sig[512+i]=a>32767?32767:a;}
  ram_write(0x20000,sig,sizeof sig);for(int k=0;k<4;k++)cfg(k,0x20000,0);
  static const int mix[4]={0,1,2,3};if(cap_start(4,mix,capbuf,MAXV))return 1;
  cap_wait_us(3000);keys(1);cap_mark(1);
  cap_wait_us((uint32_t)((512+lengths[b]+256)*1000000ull/44100));
  cap_mark(2);
  for(int k=0;k<3;k++)aw(CH(k,0x28),(1<<6)|31);
  cap_mark(3);cap_wait_us(40000);
  uint32_t n=cap_stop();char nm[32];snprintf(nm,sizeof nm,"fo_%u",b);cap_save(nm,n);
  OUT("%s length=%d samples=%lu errors=%lu\n",nm,lengths[b],(unsigned long)n,(unsigned long)CAP.errors);
 }
 aica_quiet();out_close();return 0;
}
