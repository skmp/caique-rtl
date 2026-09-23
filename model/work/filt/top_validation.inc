 {
  auto c=cap("tests/filt_top/hw/ft");int on=1;
  while(on<(int)c.n&&std::abs(c.v[on*4+1])<100)on++;
  std::vector<int16_t> rnd(1024);uint32_t seed=3;
  for(auto &v:rnd){seed=seed*1103515245u+12345u;v=(int16_t)(seed>>16);}
  for(int k=0;k<4;k++){
   std::vector<int16_t> xi(c.n);
   for(unsigned n=on;n<c.n;n++)xi[n]=k==1?1000:k==3?rnd[(n-on)%1024]:0;
   report("top",0,k,k==3?0x1fff:0x1ffe,4,check(c,k,k==3?0x1fff:0x1ffe,4,xi,on,on));
  }
 }
