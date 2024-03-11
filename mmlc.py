import re,sys

def ptn(ptn,s):
  v = re.match(ptn,s)
  if v==None: return None
  r = [v.group()]; r.extend(v.groups()); return r

PKEYOFF="PKEYOFF"
PWAIT="PWAIT"
PTONE="PTONE"
PVOLUME="PVOLUME"
PEND="PEND"
PLOOP="PLOOP"
PNEXT="PNEXT"
def mml_parse(src):
  src =re.sub("[\\|\\s]","",src)+"\0"; pos = 0
  def readInt(default=Exception):
    nonlocal src,pos; 
    if r:= ptn("^-?[0-9]+",src[pos:]):
      pos += len(r[0])
      return int(r[0])
    if default==Exception: raise Exception("error")
    return default
  def readLen(default=Exception):
    def vlen(default):
      nonlocal src,pos
      if r:= ptn("^[0-9]+",src[pos:]):
        pos += len(r[0])
        return 0 if r[0]=="0" else 1/int(r[0])
      if default==Exception: raise Exception("error")
      return default
    nonlocal src,pos; l = vlen(default)
    while src[pos]=="^": pos+=1; l += vlen(default)
    l2 = l
    while src[pos]==".": pos+=1; l2=l2/2; l+=l2
    return l
  r = [[],[],[],[]]; ch = 0
  def o(*data): nonlocal ch,r; r[ch].extend(data)
  ls = [1/4,1/4,1/4,1/4]; os = [4,4,4,4]; vs = [15,15,15,15]; qs = [1,1,1,1]
  tones = {"c":1,"d":3,"e":5,"f":6,"g":8,"a":10,"b":12}; loops=[[],[],[],[]]
  while True:
    c = src[pos]; pos += 1
    match c:
      case "D" | "E" | "F" | "G": ch = ord(c)-ord("D")
      case "r": o(["keyoff",readLen(ls[ch])])
      case "c" | "d" | "e" | "f" | "g" | "a" | "b":
                tone = tones[c]
                match src[pos]:
                  case "+": tone+=1; pos+=1
                  case "-": tone-=1; pos+=1
                if ch==3: o(["volume",vs[ch]])
                else:o(["tone",tone+os[ch]*12])
                w=readLen(ls[ch]);q=qs[ch]
                o(["wait",w*q])
                if q!=1: o(["keyoff",w*(1-q)])
      case "l": ls[ch]=readLen()
      case "o": os[ch]=readInt()
      case "@": o(["@",readInt()])
      case "q": qs[ch]=readInt()/8
      case "t": o(["tempo",readInt()])
      case "v": vs[ch]=readInt(vs[ch]); o(["volume",vs[ch]])
      case ">": os[ch]+=1
      case "<": os[ch]-=1
      case "[": o(["loop"])
      case "]": o(["next",readInt()])
      case ";": pos+=len(ptn("[^\r\n\0]*", src[pos:])[0])
      case "\n" | "\r": pass
      case "\0": break
  print(f"/*\n{r}\n*/")
  return r

def mml_compile(name,chs):
  tempos={}; all_len = 0
  for i,ch in enumerate(chs):
    old_volume=15; at = 1; volume=0; stack = []; stackMax = 0
    r = []
    def p(*bs):
      nonlocal r
      for b in bs: r.append(f"{b}")
    def outvolume():
      nonlocal old_volume,at,volume
      v = ((at&15)<<4)|(volume&15)
      if v != old_volume: p(PVOLUME,v); old_volume=v
    t = 60*60*4/120; diff = 0.0; all = 0;all2 = 0
    p(PTONE,0,PVOLUME,15)
    for v in ch:
      match v:
        case ["wait",a] | ["keyoff", a]:
                            for t1,tm in tempos.items():
                              if t1 <= int(all2*192): t = tm
                            outvolume()
                            f=t*a+diff
                            all2+=t*a
                            n = int(f+0.5); diff = f-n; all += n
                            k = PWAIT if v[0] == "wait" else PKEYOFF
                            while n>256: p(k,0);n-=256
                            if n!=0: p(k,n)
        case ["volume",b]:volume=(15-b)
        case ["tone",b]:  outvolume();p(PTONE,b)
        case ["tempo",t]: tempos[int(all2*192)]=60*60*4/t
        case ["@",v]:     at = (v+1)
        case ["loop"]:    stack.append((len(r),all,all2));stackMax=max(len(stack),stackMax);p(PLOOP,0)
        case ["next", n]:
                          (l,al,al2)=stack.pop();r[l+1]=f"{n}"; p(PNEXT); nn=(l+2)-len(r); p(nn&255,nn>>8)
                          n-=1
                          all2+=(all2-al2)*n; all+=(all-al)*n
                          diff = all2-all
                          df2 = int(diff)
                          #print(f"df {diff} df2 {df2}",file=sys.stderr)
                          if df2>0: p(PWAIT,df2); diff-=df2
    p(PEND)
    r.insert(0,f"{stackMax}")
    print(f"u8 const {name}_{i}[{len(r)}]={{\n  {','.join(r)}}};")
    all_len += len(r)
    print(f"all {all} {all2}",file=sys.stderr)
  d = map(lambda i:f'{name}_{i},',range(len(chs)))
  print(f"u8* const {name}[]={{{''.join(d)}}};")
  all_len += 2*4
  print(f"data size {all_len}bytes.",file=sys.stderr)
def main():
  str="""
  D t280 o4 @4 v15 l8 q7 ||
  D a 1   | r4 [e  a ]3  |>c+ e 1 | ^2r4^8< ||
  D a+1   | r4 [f+ a+]3 |>c+ f+1 | ^2r4^8< ||
  D t240 >d 1< | r4 [a >d<]3 |>f+ a 1 | ^2r4^8 ||
  D t180 e4^8>e8^2^1^1r1
  E o3 @6 v15 l8 q5 ||
  E a 1   | r4 [e  a ]3  |>c+ e 1 | ^2r4^8< ||
  E a+1   | r4 [f+ a+]3  |>c+ f+1 | ^2r4^8< ||
  E >d 1< | r4 [a >d<]3  |>f+ a 1 | ^2r4^8 ||
  E e4^8>e8^2^1^1r1
  F o1 @14 v15l4 q6 || a1^1^1^2g+2
  F f+1^1^1 f+2c+2 || d1^1^1^1 || e1^1^1r1
  """
  mml_compile("bgm1",mml_parse(str))

main()
