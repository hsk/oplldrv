import re,sys

def read_all(filename):
  fp = open(filename, "r")
  if fp == None: return None
  img = fp.read()  # ファイル終端まで全て読んだデータを返す
  fp.close()
  return img

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
  class Ch:
    def __init__(self,no):
      self.no = no
      self.l = 1/4
      self.o = 4
      self.v = 15
      self.q = 1
      self.loop=[]
  chs = []; r=[]
  for i in range(9): chs.append(Ch(i));r.append([])
  def o(*data): nonlocal ch,r; r[ch.no].extend(data)
  tones = {"c":1,"d":3,"e":5,"f":6,"g":8,"a":10,"b":12}
  ch = None
  while True:
    c = src[pos]; pos += 1
    match c:
      case "D" | "E" | "F" | "G": ch = chs[ord(c)-ord("D")]
      case "r": o(["keyoff",readLen(ch.l)])
      case "c" | "d" | "e" | "f" | "g" | "a" | "b":
                tone = tones[c]
                match src[pos]:
                  case "+": tone+=1; pos+=1
                  case "-": tone-=1; pos+=1
                o(["tone",tone+ch.o*12])
                w=readLen(ch.l);q=ch.q
                o(["wait",w*q])
                if q!=1: o(["keyoff",w*(1-q)])
      case "l": ch.l=readLen()
      case "o": ch.o=readInt()
      case "@": o(["@",readInt()])
      case "q": ch.q=readInt()/8
      case "t": o(["tempo",readInt()])
      case "v": ch.v=readInt(ch.v); o(["volume",ch.v])
      case ">": ch.o+=1
      case "<": ch.o-=1
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
    def outwait(fk,k,a):
      def p2(a):
        nonlocal fk,k
        if fk: p(fk,a)
        else: p(a)
        fk=k
      nonlocal tempos,t,diff,all,all2
      for t1,tm in tempos.items():
        if t1 <= int(all2*192): t = tm
      f=t*a+diff
      all2+=t*a
      n = int(f+0.5); diff = f-n; all += n
      if n==0: print(f"{k},{a}",file=sys.stderr); return
      while n>=256: p2(0);n-=256
      if n!=0: p2(n)

    vi = 0
    while vi<len(ch):
      v = ch[vi]; vi += 1
      match v:
        case ["wait",a] | ["keyoff", a]:
                            outvolume()
                            k = PWAIT if v[0] == "wait" else PKEYOFF
                            outwait(k,k,a)
        case ["volume",b]:volume=(15-b)
        case ["tone",b] if ch[vi][0]!="wait": print("error!!")
        case ["tone",b]: outvolume();p(b);outwait(False,PWAIT,ch[vi][1]); vi+=1
        case ["tempo",t]: tempos[int(all2*192)]=60*60*4/t
        case ["@",v]:     at = (v+1)
        case ["loop"]:    stack.append((len(r),all,all2));stackMax=max(len(stack),stackMax);p(PLOOP,0)
        case ["next", n]:
                          (l,al,al2)=stack.pop();r[l+1]=f"{n}"; p(PNEXT); nn=((l+2)-len(r))&0xffff; p(nn&255,nn>>8)
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
  str = read_all(sys.argv[1])
  mml_compile(sys.argv[2],mml_parse(str))

main()
