#include "../../tools/filt_capture.h"
#include <algorithm>
#include <cstdlib>
using I=int64_t;
int main(int argc,char**argv){int batch=argc>1?atoi(argv[1]):0,slot=argc>2?atoi(argv[2]):1,ev=argc>3?atoi(argv[3]):924;
 auto c=cap("tests/filt_overflow/hw/fo_"+std::to_string(batch));int on=1;while(!c.v[on*4+3])on++;
 I L=-I(c.v[(on-1)*4+slot])/2,B=-1;int k=slot==0?512:slot==1?510:508;int bad=0;
 for(int n=on;n<(int)c.n;n++){
  I x=c.v[n*4+3]/2,q=n<ev?192:13,D=-2*((-q*B)>>8),H=x-L-D;
  B+=(k*H)>>9;L-=(-k*B)>>9;I p=std::clamp<I>(-2*L,-524288,524287),y=c.v[n*4+slot];
  if(n<on+8||n==on+15||n==on+127||(p!=y&&bad++<30))printf("n=%d (+%d) x=%lld D=%lld H=%lld B=%lld L=%lld predicted=%lld actual=%lld diff=%lld\n",n,n-on,(long long)x,(long long)D,(long long)H,(long long)B,(long long)L,(long long)p,(long long)y,(long long)(p-y));
 }
}
