 std::vector<int> cf,cq;
 for(int F:{0x1c00,0x1e00})for(int Q=0;Q<32;Q++){cf.push_back(F);cq.push_back(Q);}
 for(int e=11;e<=15;e++)for(int m:{0,1,2,3,0x55,0xaa,0xff,0x100,0x155,0x1fe,0x1ff}){cf.push_back((e<<9)|m);cq.push_back(4);}
 auto ci=input("tests/filt_coef/hw/input.bin");
 for(int batch=0;batch<30;batch++){
  auto c=cap("tests/filt_coef/hw/fi_"+std::to_string(batch));int on=1;
  for(;on<(int)c.n;on++){bool found=false;for(int k=0;k<4&&batch*4+k<(int)cf.size();k++)found|=std::abs(c.v[on*c.ns+k])>100;if(found)break;}
  if(on==(int)c.n)throw std::runtime_error("missing coef impulse");
  for(int k=0;k<4&&batch*4+k<(int)cf.size();k++){int i=batch*4+k;report("coef",batch,k,cf[i],cq[i],check(c,k,cf[i],cq[i],ci,on,64));}
 }
