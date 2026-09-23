# scratch: which rounding schemes have a zero-input output cycle (0,-1,-1) at e=15, q=1 (k=504, 510)
import itertools, sys
def rnd(v,sh,m):
    if sh<=0: return v<<-sh
    a=abs(v); h=1<<(sh-1); M=(1<<sh)-1
    if m==0: return v>>sh
    if m==1: r=a>>sh
    elif m==2: r=(a+h)>>sh
    elif m==3: return (v+h)>>sh
    elif m==4: return -((-v)>>sh)
    elif m==5: r=(a+M)>>sh
    return -r if v<0 else r
def cycles(k,HL,HB,r1,r3,r4,order):
    found=set()
    for L0 in range(-3<<HL,3<<HL):
        for b0 in range(-40<<HB,40<<HB):
            L,b=L0,b0
            seen={}
            for i in range(60):
                st=(L,b)
                if st in seen:
                    per=i-seen[st]; break
                seen[st]=i
                if order==0:
                    b=b+rnd(k*(0-L),9+HL-HB,r1)-rnd(k*128*b,16,r3)
                    L=L+rnd(k*b,9+HB-HL,r4)
                else:
                    L=L+rnd(k*b,9+HB-HL,r4)
                    b=b+rnd(k*(0-L),9+HL-HB,r1)-rnd(k*128*b,16,r3)
            else: continue
            # collect cycle outputs
            outs=[]
            for j in range(per):
                if order==0:
                    b=b+rnd(k*(0-L),9+HL-HB,r1)-rnd(k*128*b,16,r3); L=L+rnd(k*b,9+HB-HL,r4)
                else:
                    L=L+rnd(k*b,9+HB-HL,r4); b=b+rnd(k*(0-L),9+HL-HB,r1)-rnd(k*128*b,16,r3)
                outs.append(L>>HL)
            # canonical rotation
            rots=[tuple(outs[i:]+outs[:i]) for i in range(len(outs))]
            found.add(min(rots))
    return found
want=(-1,-1,0)
hits=[]
for HL in range(0,3):
  for HB in range(0,3):
    for r1,r3,r4 in itertools.product(range(6),repeat=3):
      for order in (0,1):
        ok=True
        for k in (504,510):
            c=cycles(k,HL,HB,r1,r3,r4,order)
            if want not in c: ok=False;break
        if ok:
            hits.append((HL,HB,r1,r3,r4,order)); print('HIT',HL,HB,r1,r3,r4,order, cycles(504,HL,HB,r1,r3,r4,order)); sys.stdout.flush()
print(len(hits),'hits')
