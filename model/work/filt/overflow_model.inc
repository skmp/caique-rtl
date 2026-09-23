#ifdef VERIFY_MODEL
  if(n==(int)c.n){
   caique::AicaModel m;m.write(0x28,64);m.write(0x20,240);
   auto &s=m.slot[0];s.enabled=true;s.AEG.off=true;s.update_rate=0;s.FEG.v=F;
   s.lpf_low=-I(c.v[(on-1)*4+slot])/2;s.lpf_band=b;
   for(int t=on;t<(int)c.n;t++){
    if(t==event)m.write(0x28,64|31);
    s.s0=s.s1=c.v[t*4+3]/16;m.step();
    if(m.MIXS[0]!=c.v[t*4+slot]){
     fprintf(stderr,"PRODUCTION FAIL slot=%d F=%04x n=%d expected=%d actual=%d\n",slot,F,t,c.v[t*4+slot],m.MIXS[0]);exit(2);
    }
   }
  }
#endif
