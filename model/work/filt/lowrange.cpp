// scratch: range of low (= -MIXS/2) per filt_low stream at a few times after the step
#include "../../tools/filt_capture.h"
int main(){for(int b=0;b<6;b++){auto c=cap("tests/filt_low/hw/fl_"+std::to_string(b));unsigned on=1;while(on<c.n&&!c.v[on*4+3])on++;
 for(int k=0;k<3;k++){printf("fl_%d s%d:",b,k);for(unsigned t:{0u,1000u,10000u,40000u,80000u,120000u,170000u})if(on+t<c.n)printf(" +%u:%d",t,-c.v[(on+t)*4+k]/2);printf("\n");}}}
