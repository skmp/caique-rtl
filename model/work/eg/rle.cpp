#include "../../tools/filt_capture.h"   // scratch: build to build/work/ (see HANDOVER)
#include <cstdlib>
int main(int argc,char**argv){
  auto c=cap(argv[1]);int k=atoi(argv[2]);
  FILE*f=fopen((std::string(argv[1])+".hdr").c_str(),"rb");uint32_t h[200]={0};size_t nh=fread(h,4,200,f);fclose(f);
  printf("marks:");for(size_t i=11;i+1<nh&&i<11+2*h[6];i+=2)printf(" %u@%d",h[i],(int)(h[i+1]-c.first));puts("");
  printf("stream %d runs (start:value*len):\n",k);
  int prev=c.v[k];unsigned start=0;int printed=0;
  for(unsigned i=1;i<=c.n;i++){int cur=i<c.n?c.v[i*c.ns+k]:0x7fffffff;if(cur!=prev){printf(" %u:%d*%u",start,prev,i-start);if(++printed%6==0)puts("");prev=cur;start=i;if(printed>400){puts(" ...");break;}}}
  puts("");
}
