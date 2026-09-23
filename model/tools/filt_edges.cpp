// Read filt_edges captures using only integer arithmetic. No fitted onset/state.
#include <cstdio>
#include <cstdint>
#include <vector>
#include <cstdlib>
int main(){
 const int cutoff[4][3]={{0x1ffe,0x1ffc,0x1e00},{0x1dfe,0x1dfc,0x1c00},{0x1bfe,0x1bfc,0x1a00},{0x19fe,0x19fc,0x1800}};
 for(int b=0;b<4;b++){
  char path[160];snprintf(path,sizeof path,"tests/filt_edges/hw/edge_%d.hdr",b);
  FILE*f=fopen(path,"rb");if(!f){perror(path);return 1;}uint32_t h[11];
  if(fread(h,4,11,f)!=11||h[0]!=0x31504143||h[1]!=4||h[4])return 2;fclose(f);
  snprintf(path,sizeof path,"tests/filt_edges/hw/edge_%d.bin",b);f=fopen(path,"rb");if(!f)return 3;
  std::vector<int32_t> data(h[2]*4);if(fread(data.data(),4,data.size(),f)!=data.size())return 4;fclose(f);
  printf("batch %d samples=%u capture_errors=%u\n",b,h[2],h[4]);int pulses=0;
  for(unsigned n=4;n+16<h[2];n++)if(data[n*4+3]){
   pulses++;printf(" input n=%u A=%d\n",n,data[n*4+3]/16);
   for(int k=0;k<3;k++){
    printf("  F=%04x low:",cutoff[b][k]);
    for(int d=-3;d<10;d++){
     int32_t v=data[(n+d)*4+k];if(v&1){fprintf(stderr,"odd output\n");return 5;}
     printf(" %d",-v/2);
    }puts("");
   }
  }
  if(pulses!=7){fprintf(stderr,"expected 7 impulses, got %d\n",pulses);return 6;}
 }
}
