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
                o(["tone",tone,readLen(ch.l)])
      case "l": ch.l=readLen()
      case "o": o(["o",readInt()])
      case "@": o(["@",readInt()])
      case "q": o(["q",readInt()/8])
      case "t": o(["tempo",readInt()])
      case "v": o(["volume",readInt()])
      case ">" | "<": o([c])
      case "[": o(["loop"])
      case "]": o(["next",readInt()])
      case ";": pos+=len(ptn("[^\r\n\0]*", src[pos:])[0])
      case "\n" | "\r": pass
      case "\0": break
  print(f"/*\n{r}\n*/")
  return r

def mml_compile(name,chs):
  class G:pass
  G.tempos={}; G.all_len = 0
  for i,ch in enumerate(chs):
    
    G.old_volume=15; G.r = []; G.at = 1
    G.volume=0; G.stack = []; G.stackMax = 0
    def p(*bs):
      for b in bs: G.r.append(f"{b}")
    def outvolume():
      v = ((G.at&15)<<4)|(G.volume&15)
      if v != G.old_volume: p(PVOLUME,v); G.old_volume=v
    G.t = 60*60*4/120; G.diff = 0.0; G.all = 0;G.all2 = 0
    def outwait(fk,k,a):
      def p2(a):
        nonlocal fk,k
        if fk: p(fk,a)
        else: p(a)
        fk=k
      for t1,tm in G.tempos.items():
        if t1 <= int(G.all2*192): G.t = tm
      f=G.t*a+G.diff
      G.all2+=G.t*a
      n = int(f+0.5); G.diff = f-n; G.all += n
      if n==0: print(f"{k},{a}",file=sys.stderr); return
      while n>=256: p2(0);n-=256
      if n!=0: p2(n)
    def cmd_compile(v):
      nonlocal vi
      match v:
        case ["wait",a] | ["keyoff", a]:
                          outvolume()
                          k = PWAIT if v[0] == "wait" else PKEYOFF
                          outwait(k,k,a)
        case ["volume",b]:G.volume=(15-b)
        case ["tone",b,w]:
                          outvolume()
                          p(f"/*PTONE,*/{b+G.o*12}")
                          outwait(False,PWAIT,w*G.q)
                          if G.q!=1: outwait(PKEYOFF,PKEYOFF,w*(1-G.q))
        case ["q",q]:     G.q=q
        case ["o",o]:     G.o=o
        case [">"]:       G.o+=1
        case ["<"]:       G.o-=1
        case ["tempo",t]: G.tempos[int(G.all2*192)]=60*60*4/t
        case ["@",v]:     G.at = (v+1)
        case ["loop"]:    G.stack.append((len(G.r),G.all,G.all2));G.stackMax=max(len(G.stack),G.stackMax);p(PLOOP,0)
        case ["next", n]:
                          (l,al,al2)=G.stack.pop();G.r[l+1]=f"{n}"; p(PNEXT); nn=((l+2)-len(G.r))&0xffff; p(nn&255,nn>>8)
                          n-=1
                          G.all2+=(G.all2-al2)*n; G.all+=(G.all-al)*n
                          G.diff = G.all2-G.all
                          df2 = int(G.diff)
                          if df2>0: p(PWAIT,df2); G.diff-=df2
        case ["q", q]:    G.q=q
    vi = 0
    while vi<len(ch):
      v = ch[vi]; vi += 1; cmd_compile(v)
    p(PEND)
    G.r.insert(0,f"{G.stackMax}")
    print(f"u8 const {name}_{i}[{len(G.r)}]={{\n  {','.join(G.r)}}};")
    G.all_len += len(G.r)
    print(f"G.all {G.all} {G.all2}",file=sys.stderr)
  d = map(lambda i:f'{name}_{i},',range(len(chs)))
  print(f"u8* const {name}[]={{{''.join(d)}}};")
  G.all_len += 2*4
  print(f"data size {G.all_len}bytes.",file=sys.stderr)

def main():
  str = read_all(sys.argv[1])
  mml_compile(sys.argv[2],mml_parse(str))

main()
