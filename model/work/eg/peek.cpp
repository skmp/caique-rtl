#include "../../tools/filt_capture.h"   // scratch: build to build/work/ (see HANDOVER)
#include <cstdlib>
int main(int argc,char**argv){
  auto c=cap(argv[1]);int ref=argc>2?atoi(argv[2]):3;int from=argc>3?atoi(argv[3]):-1;
  unsigned on=1;while(on<c.n&&!c.v[on*c.ns+ref])on++;
  if(from<0)from=on-3;
  printf("%s ns=%u n=%u first=%u onset(ref %d)=%u ring c0? see hdr\n",argv[1],c.ns,c.n,c.first,ref,on);
  for(int i=from;i<from+12&&i<(int)c.n;i++){printf("  i=%d n=%u:",i,c.first+i);for(unsigned k=0;k<c.ns;k++)printf(" %8d",c.v[i*c.ns+k]);puts("");}
}
