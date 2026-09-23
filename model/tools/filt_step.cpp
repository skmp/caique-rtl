// C++ replacement for filt_step.py. Exports the existing filt_cyc captures with
// their verified onsets; byte-identical step.bin and decay.bin, no Python/FPU.
#include "filt_capture.h"
#include <filesystem>
int main(){
 std::filesystem::create_directories("work/filt");
 FILE *s=fopen("work/filt/step.bin","wb"),*d=fopen("work/filt/decay.bin","wb");
 if(!s||!d)return 1;
 for(int i=0;i<24;i++){
  const auto&cf=cyc_config[i];auto c=cap("tests/filt_cyc/hw/fc_"+std::to_string(i/4));
  if(c.ns!=4||c.n<unsigned(cf.on+1650))return 2;
  std::vector<int32_t> y(c.n);
  for(unsigned n=0;n<c.n;n++){int32_t v=c.v[n*4+i%4];if(v&1)return 3;y[n]=-v/2;}
  int32_t hs[]={cf.F,cf.Q,cf.A,cf.on,(int32_t)c.n},hd[]={cf.F,cf.Q,cf.A,1500};
  if(fwrite(hs,4,5,s)!=5||fwrite(y.data(),4,y.size(),s)!=y.size()||
     fwrite(hd,4,4,d)!=4||fwrite(y.data()+cf.on+150,4,1500,d)!=1500)return 4;
 }
 bool ok=fclose(s)==0;ok=(fclose(d)==0)&&ok;
 puts("exported 24 streams to work/filt/{step,decay}.bin");return !ok;
}
