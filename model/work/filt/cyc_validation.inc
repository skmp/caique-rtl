 for(int i=0;i<24;i++){
  const auto&cf=cyc_config[i];auto c=cap("tests/filt_cyc/hw/fc_"+std::to_string(i/4));
  std::vector<int16_t> xi(256,cf.A);
  report("cyc",i/4,i%4,cf.F,cf.Q,check(c,i%4,cf.F,cf.Q,xi,cf.on,0));
 }
