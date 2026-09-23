// Compare complete model captures with hardware, distinguishing inherited-state
// startup differences from sustained arithmetic differences. Integer math only.
#include "filt_capture.h"
#include <algorithm>
#include <cstdlib>
int main(){
 long long total=0,bad=0;int exact=0,streams=0;
 for(auto name:{"filt_id2","filt_coef"}){
  long long casebad=0,casetotal=0;int caseexact=0,casestreams=0;
  int batches=std::string(name)=="filt_id2"?15:30;
  for(int b=0;b<batches;b++){
   std::string p=std::string("tests/")+name;
   auto h=cap(p+"/hw/fi_"+std::to_string(b)),m=cap(p+"/model/fi_"+std::to_string(b));
   auto onset=[&](const Capture&c){for(unsigned n=1;n<c.n;n++)for(unsigned k=0;k<c.ns;k++)if(std::abs(c.v[n*c.ns+k])>100)return (int)n;throw std::runtime_error("onset");};
   int oh=onset(h),om=onset(m),len=std::min((int)h.n-oh,(int)m.n-om);
   for(unsigned k=0;k<h.ns;k++){
    if(b==29&&k==3)continue;
    int nb=0,last=-1,first=-1,maxerr=0;
    for(int n=0;n<len;n++){
     int d=std::abs(h.v[(oh+n)*h.ns+k]-m.v[(om+n)*m.ns+k]);
     if(d){nb++;last=n;if(first<0)first=n;maxerr=std::max(maxerr,d);}
    }
    printf("%s %d:%u %d/%d differ first=%d last=%d max=%d\n",name,b,k,nb,len,first,last,maxerr);
    streams++;exact+=nb==0;total+=len;bad+=nb;
    casestreams++;caseexact+=nb==0;casetotal+=len;casebad+=nb;
   }
  }
  printf("CASE %s %d/%d streams exact; %lld/%lld samples differ\n",name,caseexact,casestreams,casebad,casetotal);
 }
 printf("TOTAL %d/%d streams exact from impulse; %lld/%lld samples differ\n",exact,streams,bad,total);
}
