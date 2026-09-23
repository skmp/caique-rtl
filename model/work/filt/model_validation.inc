#ifdef VERIFY_MODEL
 if(best.matched==best.total) {
  caique::AicaModel m;
  m.write(0x28,64|Q);m.write(0x20,240);
  auto &s=m.slot[0];s.enabled=true;s.AEG.off=true;s.update_rate=0;s.FEG.v=F;
  s.lpf_low=-I(c.v[(onset-1)*c.ns+stream])/2;s.lpf_band=best.band;
  for(int n=onset;n<(int)c.n;n++){
   int ix=n-onset+input_on;if(wrap)ix&=65535;
   s.s0=s.s1=ix>=0&&ix<(int)in.size()?in[ix]:0;
   m.step();
   if(m.MIXS[0]!=c.v[n*c.ns+stream]){
    fprintf(stderr,"PRODUCTION mismatch F=%04x Q=%d n=%d model=%d hardware=%d\n",F,Q,n,m.MIXS[0],c.v[n*c.ns+stream]);
    best.matched=n-onset;break;
   }
  }
 }
#endif
