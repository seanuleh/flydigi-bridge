import collections, re, sys
TOK = re.compile(r'([A-Za-z][A-Za-z0-9 ]*?)=([-\d.]+(?:,[-\d.]+)?)')
gstate=set(); gvals={}
bit_on=collections.Counter(); btn_on=collections.Counter(); both=collections.Counter(); N=0; stick=[]
for l in open(sys.argv[1]):
    f=l.split(None,3)
    if len(f)<3: continue
    if f[1]=='G':
        gstate=set(); gvals={}
        for k,v in TOK.findall(f[3] if len(f)>3 else ''):
            k=k.strip(); gvals[k]=v
            if ',' in v:
                x,y=map(float,v.split(','))
                for nm,val in ((k+'.x',x),(k+'.y',y)):
                    if val>0.5: gstate.add(nm+'+')
                    if val<-0.5: gstate.add(nm+'-')
            elif float(v)>0.5: gstate.add(k)
    elif f[1]=='P':
        g=l.split()
        if g[7]!='32': continue
        b=bytes.fromhex(g[10]); N+=1
        bits=set((i,m) for i in list(range(2,7))+list(range(20,24)) for m in range(8) if b[i]>>m&1)
        for x in bits: bit_on[x]+=1
        for gg in gstate: btn_on[gg]+=1
        for x in bits:
            for gg in gstate: both[(x,gg)]+=1
        stick.append((b[24:30].hex(' '), gvals.get('Left Thumbstick','-'), gvals.get('Right Thumbstick','-'), gvals.get('Left Trigger','-'), gvals.get('Right Trigger','-')))
J=lambda x,g: both[(x,g)]/(bit_on[x]+btn_on[g]-both[(x,g)])
print("packets",N)
for x in sorted(bit_on):
    best=sorted(((J(x,g),g) for g in btn_on), reverse=True)[:2]
    print(f"byte{x[0]:2} 0x{1<<x[1]:02x} n={bit_on[x]:4}  ->", ", ".join(f"{g} {j:.2f}" for j,g in best))
print("GC inputs w/o radio bit:", [g for g in btn_on if max([J(x,g) for x in bit_on] or [0])<0.5])
for s in stick[::max(1,len(stick)//40)]:
    if s[1]!='-' or s[2]!='-' or s[3]!='-' or s[4]!='-': print(s)
