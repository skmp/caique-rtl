/* Independent cutoff endpoint probe: paired impulse amplitudes expose gain without
 * relying on an inferred band state. Each block starts with 512 zero samples.
 * Fourth stream bypasses the filter and records the exact input timing. */
#include "cap.h"
#define NS 4
#define MAXV (1u << 20)
static int32_t capbuf[MAXV];
static int16_t sig[4096];
static const uint16_t cutoff[][3] = {
    {0x1ffe,0x1ffc,0x1e00}, {0x1dfe,0x1dfc,0x1c00},
    {0x1bfe,0x1bfc,0x1a00}, {0x19fe,0x19fc,0x1800}
};
int test_main(void) {
    out_open("filt_edges.txt");
    const int amplitude[7]={1000,-1000,7,-7,1,-1,1000};
    for(int i=0;i<7;i++) sig[512+i*512]=amplitude[i];
    ram_write(0x20000,sig,sizeof sig);
    for(unsigned batch=0;batch<sizeof cutoff/sizeof cutoff[0];batch++) {
        aica_quiet();
        for(int k=0;k<NS;k++) {
            slot_cfg_t c; slot_cfg_default(&c,0x20000,4095);
            c.LPCTL=0;c.ISEL=k;c.VOFF=1;c.LPOFF=k==3;c.Q=4;
            for(int j=0;j<5;j++)c.FLV[j]=k==3?0x1e00:cutoff[batch][k];
            c.FAR=c.FD1R=c.FD2R=c.FRR=0;slot_write(k,&c);
            LOG("edge_%u stream %d F=%04x Q=4 bypass=%d\n",batch,k,c.FLV[0],c.LPOFF);
        }
        static const int mixs[NS]={0,1,2,3};
        if(cap_start(NS,mixs,capbuf,MAXV))return 1;
        cap_wait_us(3000);
        for(int k=0;k<NS;k++)aw(CH(k,0),(ar(CH(k,0))&0x3fff)|0x4000);
        aw(CH(0,0),(ar(CH(0,0))&0x7fff)|0x8000);
        cap_mark(1);cap_wait_us(110000);
        uint32_t n=cap_stop();char name[32];snprintf(name,sizeof name,"edge_%u",batch);
        cap_save(name,n);OUT("%s samples=%lu errors=%lu\n",name,(unsigned long)n,(unsigned long)CAP.errors);
    }
    aica_quiet();out_close();return 0;
}
