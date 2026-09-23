# scratch: trace a filt_search9-style hypothesis on a filt_cyc stream from the best start state
import sys; sys.path.insert(0,'tools')
import filt_lp as fl
def rnd(v,sh,m):
    if sh<=0: return v<<-sh
    a=abs(v); h=1<<(sh-1); M=(1<<sh)-1
    if m==0: return v>>sh
    if m==1: r=a>>sh
    elif m==2: r=(a+h)>>sh
    elif m==3: return (v+h)>>sh
    elif m==4: return -((-v)>>sh)
    elif m==5: r=(a+M)>>sh
    elif m==6:
        fl_=v>>sh; fr=v&M
        if fr>h or (fr==h and fl_&1): fl_+=1
        return fl_
    return -r if v<0 else r
Q128=[192,176,160,144,128,120,112,104,96,88,80,72,64,60,56,52,48,44,40,36,32,30,28,26,24,22,20,18,16,15,14,13]
def run(i,HL,HB,rm,ro,form=0,pl=0,L0=0,B0=0,dn=0,N=520,REST=0):
    F,Q,A,y=fl.stream(i); on=fl.ON[i]; ns=on-REST
    e=F>>9; k=256+((F&0x1FF)>>1); qm=Q128[Q]; sh=24-e
    L,B=L0,B0; out=[]
    for n in range(ns,ns+N):
        X8=8*A if on+dn<=n<on+dn+256 else 0
        lw=L if not pl else rnd(L,HL,pl-1)<<HL
        qt=rnd(k*qm*B,sh+7,rm[2])
        if form==0: B=B+rnd(k*X8,sh-HB,rm[0])-rnd(k*lw,sh-HB+HL,rm[1])-qt
        else: B=B+rnd(k*((X8<<HL)-lw),sh-HB+HL,rm[0])-qt
        L=L+rnd(k*B,sh+HB-HL,rm[3])
        out.append((n,rnd(L,HL,ro),y[n],L,B))
    return out
def best(i,HL,HB,rm,ro,form=0,pl=0,REST=0):
    F,Q,A,y=fl.stream(i); on=fl.ON[i]; ns=on-REST
    bst=None
    for dn in (-1,0,1):
        for a in range(-(1<<HL),2<<HL):
            L0=y[ns-1]*(1<<HL)+a
            if rnd(L0,HL,ro)!=y[ns-1]: continue
            for B0 in range(-48<<HB,(48<<HB)+1):
                o=run(i,HL,HB,rm,ro,form,pl,L0,B0,dn,REST=REST)
                m=next((j for j,t in enumerate(o) if t[1]!=t[2]),len(o))
                if bst is None or m>bst[0]: bst=(m,L0,B0,dn)
    return bst
if __name__=='__main__':
    i=int(sys.argv[1]); HL,HB=int(sys.argv[2]),int(sys.argv[3]); rm=[int(c) for c in sys.argv[4]]; ro=int(sys.argv[5])
    form=int(sys.argv[6]) if len(sys.argv)>6 else 0; pl=int(sys.argv[7]) if len(sys.argv)>7 else 0
    b=best(i,HL,HB,rm,ro,form,pl); print('best',b)
    o=run(i,HL,HB,rm,ro,form,pl,b[1],b[2],b[3])
    m=b[0]
    for t in o[max(0,m-6):m+4]: print(t)
