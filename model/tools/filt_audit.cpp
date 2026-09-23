// Independent integer audit of the measured second-order recurrence.
#include <cstdio>
#include <cstdint>
#include <vector>
#include <algorithm>
using I = int64_t;
int main(){
 FILE *f=fopen("work/filt/step.bin","rb"); if(!f)return 1;
 int h[5],idx=0; const int q[]={192,176,160,144,128};
 while(fread(h,4,5,f)==5){
  std::vector<int> y(h[4]); if(fread(y.data(),4,y.size(),f)!=y.size())return 2;
  int e=h[0]>>9,k=256+((h[0]&511)>>1),s=24-e,on=h[3];
  printf("%d F=%04x Q=%d A=%d: ",idx++,h[0],h[1],h[2]);
  for(int n=on-3;n<on+15;n++)printf("%d ",y[n]); puts("");
  for(int kk=k;kk<=k+(k==511);kk++){
   I den=I(1)<<(2*s), c=kk*kk, d=I(kk)*q[h[1]]*(I(1)<<(s-7));
   I lo=INT64_MAX,hi=INT64_MIN,err=0; int fl=0,ce=0;
   for(int n=on+2;n<on+500;n++){
    I x=n<on+256?8*h[2]:0;
    I v=c*x+(2*den-c-d)*y[n-1]+(d-den)*y[n-2];
    I r=den*y[n]-v;lo=std::min(lo,r);hi=std::max(hi,r);err+=r;
    fl+=y[n]==(v>>(2*s));ce+=y[n]==-((-v)>>(2*s));
   }
   printf(" k=%d recurrence residual [%lld,%lld]/%lld sum=%lld floor=%d ceil=%d\n",kk,(long long)lo,(long long)hi,(long long)den,(long long)err,fl,ce);
  }
 }
}
