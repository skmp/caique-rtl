// Search whether exact-product sign corrections explain apparent rounding anomalies.
// E<base><pos><neg><zero>: base 0=floor 1=ceil 2=tz 3=away;
// three exact-product digits encode -1,0,+1 as 0,1,2.
#define main rule_main
#include "filt_rule.cpp"
#undef main
int main(int argc,char**argv){
 FILE*f=fopen("work/filt/step.bin","rb");if(!f)return 1;int h[5];
 while(fread(h,4,5,f)==5){DS d{h[0],h[1],h[2],h[3],h[4],{}};d.y.resize(d.N);if(fread(d.y.data(),4,d.N,f)!=(size_t)d.N)return 2;ds.push_back(d);}fclose(f);
 int form=argc>1?atoi(argv[1]):2;K255=512;
 struct Result{int a,b,c,full=0,score=0;std::vector<int> m;};std::vector<Result> rr;
 for(int a=0;a<108;a++)for(int b=0;b<(form==2?1:108);b++)for(int c=0;c<108;c++)rr.push_back({a,b,c});
#pragma omp parallel for schedule(dynamic)
 for(size_t j=0;j<rr.size();j++){
  auto&r=rr[j];Rule a{EXACT_SIGN,r.a,""},b{EXACT_SIGN,r.b,""},c{EXACT_SIGN,r.c,""};
  for(auto&d:ds){int best=0;for(int dn=-1;dn<=1;dn++)best=std::max(best,run(d,a,b,c,Opt{form,3,0,0,0},dn));r.m.push_back(best);r.full+=best==d.N-d.on;r.score+=best;}
 }
 std::sort(rr.begin(),rr.end(),[](auto&a,auto&b){return a.full!=b.full?a.full>b.full:a.score>b.score;});
 auto name=[](int n){char s[8];snprintf(s,sizeof s,"E%d%d%d%d",n/27,(n/9)%3,(n/3)%3,n%3);return std::string(s);};
 for(int j=0;j<10;j++){auto&r=rr[j];printf("full %d/24 form %d %s/%s/%s:",r.full,form,name(r.a).c_str(),name(r.b).c_str(),name(r.c).c_str());for(auto m:r.m)printf(" %d",m);puts("");}
}
