import re,sys

def read_all(filename):
  fp = open(filename, "r")
  if fp == None: return None
  img = fp.read()  # ファイル終端まで全て読んだデータを返す
  fp.close()
  return img

PKEYOFF="PKEYOFF"
PWAIT="PWAIT"
PTONE="PTONE"
PVOLUME="PVOLUME"
PEND="PEND"
PLOOP="PLOOP"
PNEXT="PNEXT"
PBREAK="PBREAK"
PSLOAD="PSLOAD"
PSLAON="PSLAON"
PSUSON="PSUSON"
PDRUMV="PDRUMV"
def ptn(p,s,m):
  v = re.match(p,s)
  if v==None: m[:]=[""]; return False
  r = [v.group()]; r.extend(v.groups()); m[:]=r
  return True

def preprocess(src):
    pos = 0; m=[]
    macro={}
    r = {"@":[],"#":[],"9":[],"A":[],"B":[],"C":[],"D":[],"E":[],"F":[],"G":[],"H":[]}; ch = 0
    def pt(pt,m):
      nonlocal pos,src
      if ptn(pt,src[pos:],m): pos+=len(m[0]); return True
      return False
    def o(ch,*data): nonlocal r; r[ch].extend(data)
    while pos < len(src):
      if pt("^[ \t\r\n]+",m): pass
      elif pt("^;[^\r\n]*",m): pass
      elif pt("^#([^;\r\n]+)",m): o("#",m[1])
      elif pt("^(\\*[0-9]+)\s*=\s*\{([^}]*)\}",m): print(f"macro {m[1]}");macro[m[1]]=m[2].replace(" ","")
      elif pt("^@((;[^\r\n]+[\r\n]*|[^;\r\n}]+|[\r\n]+)+\})",m):
        n=m[1];r2=[];m1=[]
        while len(n)>0:
            if ptn("^;[^\r\n]+[\r\n]*|[\r\n]+|\s+",n,m1): n=n[len(m1[0]):]; continue
            if ptn("^[^;\r\n\s]+",n,m1): r2.append(m1[0]);n=n[len(m1[0]):]; continue
            print(f"error {m}")
        o("@","".join(r2))
      elif pt("^([^\s]+)\s+([^;\r\n]+)",m):list(map(lambda x:o(x,m[2].replace(" ","")),m[1]))
      else: pos+=1
    print(f"# {r['#']}")
    
    macrows={}
    for k,vs in r.copy().items():
      if k=="#":
        m=[[]]
        for v in vs:
          print(f"g {v}")
          if ptn("^macro_offset\s*\\{([^}]+)\\}",v,m):
              for wn in re.split(",",m[1]):
                kv=wn.split("=")
                macrows[kv[0]]=int(kv[1])
            
      if k=="@" or k == "#": continue
      print(f"k {k}:vs {vs}")
      for i,v in enumerate(vs):
        def macf(m):
          nonlocal macro
          return macro[m.group(0)]
        def macw(m):
          nonlocal macro
          n = int(m.group(2)) if len(m.group(2))>0 else 0
          print(f"{m.group(1)} {n}")
          print(f"{macrows[m.group(1)]}")
          return macro[f"*{macrows[m.group(1)]+n}"]
        v=re.compile("\\*([0-9]+)").sub(macf,v)
        v=re.compile("\\*([a-zA-Z])([0-9]*)").sub(macw,v)
        r[k][i]=v
    return r
def conv_voice(dt):
  d=[0,0,0,0,0,0,0,0]
  d[2] = dt[0]&0x3f
  d[3] = dt[1]&7
  d[4] = ((dt[2]&0xf)<<4) | (dt[3]&0xf)
  d[6] = ((dt[4]&0xf)<<4) | (dt[5]&0xf)
  d[2]|= ((dt[6]&3)<<6)
  d[0] = (dt[7]&0xf) | ((dt[8]&1)<<7) | ((dt[9]&1)<<6) | ((dt[10]&1)<<5)  | ((dt[11]&1)<<4)
  d[3]|= ((dt[12]&1)<<3) 
  d[5] = ((dt[13]&0xf)<<4) | (dt[14]&0xf)
  d[7] = ((dt[15]&0xf)<<4) | (dt[16]&0xf)
  d[3]|= ((dt[17]&3)<<6)
  d[1] = (dt[18]&0xf) | ((dt[19]&1)<<7) | ((dt[20]&1)<<6) | ((dt[21]&1)<<5)  | ((dt[22]&1)<<4)
  d[3]|= ((dt[23]&1)<<4)  
  return d
  
def parse_at(lines):
  r = {};m=[]
  for l in lines:
    if ptn("^v([0-9]+)=\\{([^\\}]+)\\}$",l,m):
      r["@"+m[1]]=conv_voice(list(map(int,m[2].split(","))))
  return r
def parse_sharp(lines):
  r = {"opll_mode":0}; m=[]
  for l in lines:
    if ptn("^(opll_mode|tempo)\s+([0-9]+)",l,m): r[m[1]]=int(m[2])
    elif ptn("^(title)\s*{\s*\"([^\"]+)\"\s*}",l,m): r[m[1]]=m[2]
  return r
def parse_channel(ch,src,drum):
  r = []; l=48; pos = 0
  def readInt(default=Exception):
    nonlocal src,pos; r=[]
    if ptn("^-?[0-9]+",src[pos:],r): pos += len(r[0]); return int(r[0])
    if default==Exception: raise Exception(f"error channel {ch} pos {pos}\n{src[pos:pos+10]}")
    return default
  def readLen(c,default=Exception):
    def vlen():
      nonlocal src,pos; r=[]
      if ptn("^[0-9]+",src[pos:],r):
          pos += len(r[0]); return 0 if r[0]=="0" else 192/int(r[0])
      if ptn("^%[0-9]+",src[pos:],r):pos += len(r[0]); return int(r[0][1:])
      return None
    def vlen2():
      nonlocal src,pos
      l = vlen()
      if l == None: return None
      while src[pos]==".": pos+=1; l*=1.5
      return l
    nonlocal src,pos; spos = pos
    l = vlen2()
    if l==None:
      if default!=Exception: return default
      raise Exception("error"+src[pos:pos+10])
    while src[pos]=="^":
      pos+=1
      if src[pos] == c: pos+=1
      l2 = vlen2()
      if l2==None: raise Exception("error"+src[pos:pos+10])
      l += l2
    return l
  def o(*data): nonlocal r; r.append(list(data))
  def err():
    nonlocal pos,ch,src
    pos -= 1
    print(f"error channel {ch} pos{pos} '{src[pos]}'")
    print(f"{src[max(pos-5,0):min(pos+20,len(src))]}")
    for i in range(min(5,max(pos-5,0))+1): print("^",end="")
    print("")
    exit(-1)
  m = [""]
  while True:
    c = src[pos]; pos += 1
    match c:
      case "\0": break
      case ("b"|"s"|"m"|"c"|"h") if drum:
        s = set([c])
        while True:
          if ptn("[bsmch]",src[pos],m) and not m[0] in s: s.add(m[0]);pos+=1
          else: break
        flag = 0
        for c in s: flag |= 0x10>>["b","s","m","c","h"].index(c)
        if src[pos]==":": pos+=1; o("drum:",flag)
        else: o("drum", flag, readLen("",l))
      case "v" if drum and ptn("^([bsmch])([+-]?)([0-9]+)",src[pos:],m):
        pos+=len(m[0])
        o("drum_v",m[1],m[2],m[3])
      case ("c" | "d" | "e" | "f" | "g" | "a" | "b" | "r") if not drum:
        match src[pos]:
          case "#": c+="+"; pos += 1
          case "+": c+="+"; pos += 1
          case "-": c+="-"; pos += 1
        ln=readLen(c,l)
        #if ln > 256: print("invalid length"); err()
        o("tone",c,ln)
      case "r" if drum:
        ln=readLen(c,l)
        #if ln > 256: print("invalid length"); err()
        o("drum",0,ln)
      case "l": l=readLen(c,l); o("l",l)
      case "v" if ptn("^([+-])",src[pos:],m):
        pos+=1; o(c+m[1],(-1 if m[1]=="-" else 1)*readInt())
      case "@" | "o" | "]" | "v" | "q" | "t": o(c,readInt())
      case "s":
        if src[pos]=="o": pos+=1; o("so")
        elif src[pos]=="f": pos+=1; o("sf")
      case "[" | "<" | ">" | "&" | "|": o(c)
      case "(": n=-readInt(1);o("v-" if n < 0 else "v+",n)
      case ")": n=readInt(1);o("v-" if n < 0 else "v+",n)
      case "\\": o(c,readInt())
      case _: err()
  return r
def parse(txt):
  pp = preprocess(txt)
  r={}
  for ch,lines in pp.items():
    match ch:
      case "@" | "#": r[ch]=lines
      case _: r[ch] = "".join(lines)
  pp=r
  print(pp)
  for ch,lines in pp.items():
    match ch:
      case "@": r[ch]=parse_at(lines)
      case "#": r[ch]=parse_sharp(lines)
      case _:   r[ch]=parse_channel(ch,"".join(lines)+"\0",r["#"]["opll_mode"] and ch=="F")
  return r
def loop_expand(chs):
  class G:
    volume=15
    octave=5
    before=0
    after=0
  def expand(n,ch):
    G.before+=len(ch)
    G.volume=15; G.octave=4
    r = []
    stack = []
    i = -1
    while i+1 < len(ch):
      i+=1
      v = ch[i]
      if True:
        match v:
          case ["v",n]: G.volume=n
          case ["v-",n]:G.volume-=n
          case ["v+",n]:G.volume+=n
          case ["o",n]: G.octave=n-1
          case ["<"] if 0<G.octave: G.octave-=1
          case [">"] if G.octave<7: G.octave+=1
          case ["["]: stack.append([len(r),None,G.volume,G.octave])
          case ["|"]: stack[-1][1]=len(r)
          case ["]",n]:
            [start,br,vol,octave]= stack.pop()
            if br == None: br=len(r)
            G.octave=octave
            if (G.volume != vol or G.octave != octave) and n!=0: # 状態が違うので展開する
              before=len(r)
              loop1=r[start+1:br]
              loop=loop1+r[br+1:]
              if len(loop1)==len(loop):
                print(f"  expand loop {len(r)-start} to ({len(loop)+2}-2)*{n}={len(loop)*n}",file=sys.stderr)
              else:
                print(f"  expand loop {len(r)-start} to {len(loop)}*({n}-1)+{len(loop1)}={len(loop)*(n-1)+len(loop1)}",file=sys.stderr)                
              r[:]=r[:start]
              for j in range(n):
                #print(f'  expand: {f"loop1({len(loop1)})" if j==n-1 else f"loop({len(loop)})"}',file=sys.stderr)
                r.extend(loop1 if j==n-1 else loop)
              after=len(r)
              G.after += after-before
              continue
      r.append(v)
    return r
  r = {}
  for n,ch in chs.items():
    if n=="@" or n=="#" or ch==None: r[n]=ch; continue
    r[n]= expand(n,ch)
  print(f"loop expand {G.before}+{G.after} to {G.before+G.after}commands +{G.after/G.before*100:0.2f}%",file=sys.stderr)
  return r

def mml_compile(name,chs):
  print(chs)
  print("*/")
  class G:pass
  G.tempos={}; G.all_len = 0; G.sounds={}; G.n2i={}; G.i2n={}
  if len(chs["@"].keys())>0:
    ch = []; i = 0
    for k,ss in chs["@"].items(): ch.extend(map(str,ss));G.sounds[k]=i;i+=1
    print(f"u8 const {name}_sound[{len(ch)}]={{{','.join(ch)}}};")
  i = -1
  for n,ch in chs.items():
    if n=="@" or n=="#": continue
    i+=1
    G.n2i[n]=i
    G.i2n[i]=n
    G.old_volume=15; G.r = []; G.at = 1
    G.volume=0; G.stack = []; G.stackMax = 0; G.o=4; G.slar=False
    G.old_drum_v=[255,255,255]; G.drum_v={"b":15,"s":15,"m":15,"c":15,"h":15}
    def p(*bs):
      for b in bs: G.r.append(f"{b}")
    def outvolume():
      v = ((G.at&15)<<4)|(G.volume&15)
      if v != G.old_volume: p(PVOLUME,v); G.old_volume=v
    def out_drum_volume(v):
      v0=15-G.drum_v["b"]
      v1=(((15-G.drum_v["h"])&15)<<4)|((15-G.drum_v["s"])&15)
      v2=(((15-G.drum_v["m"])&15)<<4)|((15-G.drum_v["c"])&15)
      #print(f"drum volume {v0:02x} {v1:02x} {v2:02x}",file=sys.stderr)
      if (v & 1) and v0 != G.old_drum_v[0]: print(f"drum_v0 {v0:02x}",file=sys.stderr); p(PDRUMV,0x36,v0); G.old_drum_v[0]=v0
      if ((v&2) or (v&16)) and v1 != G.old_drum_v[1]: print(f"drum_v1 {v2:02x}",file=sys.stderr); p(PDRUMV,0x37,v1); G.old_drum_v[1]=v1
      if ((v&4) or (v&8)) and v2 != G.old_drum_v[2]: print(f"drum_v2 {v2:02x}",file=sys.stderr); p(PDRUMV,0x38,v2); G.old_drum_v[2]=v2
    G.t = 60*60*4/(chs["#"]["tempo"] if "tempo" in chs["#"] else 120)
    G.all = 0;G.all2 = 0; G.q=1
    
    def outwait(prm, fk,k,a):
      def p2(a):
        nonlocal fk,k
        if fk: p(fk,a)
        else: p(a)
        fk=k
      for t1,tm in G.tempos.items():
        if t1 <= int(G.all2*192): G.t = tm

      diff = G.all2-G.all
      #if abs(diff) >= 1:print(f"diff {prm} {diff}",file=sys.stderr)
      f=G.t*a+diff
      G.all2+=G.t*a
      n = int(f); G.all += n
      if n==0: return
      while n>=256: p2(0);n-=256
      if n!=0: p2(n)
    def cmd_compile(name,v):
      nonlocal vi
      match v:
        case ["tone","r",a]:
                      outvolume()
                      outwait("r",PWAIT,PWAIT,a/192)
        case ["v",b] if name=="F":
                      for k in G.drum_v.keys(): G.drum_v[k]=b
        case ["v",b]: G.volume=(15-b)
        case ["tone",b,w]:
                      notes={"c":0,"c+":1,"d":2,"d+":3,"e-":3,"e":4,"f":5,"f+":6,"g":7,"g+":8,"a":9,"a+":10,"b-":10,"b":11,"r":12}
                      b=notes[b];w = w/192
                      #print(f"w {w} q {G.q}")
                      outvolume()
                      p(f"/*PTONE,*/{b+G.o*12}")
                      outwait(f"tone {b}", False,PWAIT,w*G.q)
                      if G.q!=1: outwait(f"off {b}",PKEYOFF,PKEYOFF,w*(1-G.q))
        case ["l",l]: G.l=l
        case ["q",q]: G.q=q/8
        case ["o",o]: G.o=o-1
        case [">"]:
                      if G.o<7:G.o+=1
        case ["<"]:
                      if G.o>0:G.o-=1
        case ["t",t]: G.t=60*60*4/t; G.tempos[int(G.all2*192)]=G.t; print(f"t {G.all2*192}",file=sys.stderr)
        case ["@",v]  if v < 15: G.at = (v+1)
        case ["@",v]: G.at=0; p(PSLOAD,G.sounds[f"@{v}"]*8)
        case ["["]:   
                      diff = max(0,G.all2-G.all) #+0.00000001
                      G.all+=diff
                      G.stack.append([len(G.r),G.all,G.all2,None,None,None,diff])
                      G.stackMax=max(len(G.stack),G.stackMax);p(PLOOP,0,0)
        case ["]",n]: # n回ループする
                      n1=n
                      if n<2:n=1
                      [l,al,al2,br,bral,bral2,diff]=G.stack.pop();G.r[l+1]=f"{n1>>1}";G.r[l+2]=f"{n1}"
                      p(PNEXT); nn=(l-len(G.r))&0xffff; p(nn&255,nn>>8)
                      n-=1
                      if br: n-=1
                      G.all2+=(G.all2-al2)*n; G.all+=(G.all-al)*n
                      if br: G.all+=bral;G.all2+=bral2
                      G.all-=diff
                      diff=G.all2-G.all
                      diff1=int(diff)
                      p(diff1,n1)
                      G.all+=diff1
                      print(f"  [ {al2-al} ]{n1} {diff1}",file=sys.stderr)
                      #outwait(f"]{n+1+int(bool(br))}",PWAIT,PWAIT,0)
                      if br: # ブレイクアドレス
                        pos = len(G.r) - br - 2
                        G.r[br  ]= f"{pos&255}"
                        G.r[br+1]= f"{pos>>8}"
        case ["q",q]: G.q=q
        case ["v-",v]:G.volume+=v
        case ["v+",v]:G.volume+=v
        case ["|"]:
                      #print("|",file=sys.stderr)
                      if G.stack[-1][3] != None: print("error arleady use |")
                      G.stack[-1][3]=len(G.r)+1
                      G.stack[-1][4]=G.all-G.stack[-1][1]
                      G.stack[-1][5]=G.all2-G.stack[-1][2]
                      p(PBREAK,None,None)
        case ["drum",v,w]: w=w/192;out_drum_volume(v);p(f"/*PDRUM*/{v+0x60}");outwait(f"drum {v}",None,PWAIT,w)
        case ["drum_v",a,"+",n]: G.drum_v[a]+=int(n)
        case ["drum_v",a,"-",n]: G.drum_v[a]-=int(n)
        case ["drum_v",a,"",n]: G.drum_v[a]=int(n)
        case ["&"]: p(PSLAON)
        case ["so"]: p(PSUSON)
        case v:       print(f"unknown {v}")
    vi = 0
    while vi<len(ch):
      v = ch[vi]; vi += 1; cmd_compile(n,v)
    p(PEND)
    G.r.insert(0,f"{G.stackMax}")
    split=",\n  "
    print(f"u8 const {name}_{i}[{len(G.r)}]={{\n  {split.join(G.r)}}};")
    G.all_len += len(G.r)
    print(f"{n} all {G.all} {G.all2}",file=sys.stderr)
    
  d = list(map(lambda i:f'{name}_{i},',range(i+1)))
  if "F" in G.n2i and G.n2i["F"]!=6:
    print(f"n2i {list(G.n2i.items())}")
  d.insert(0,f"(u8*){len(d)|(chs['#']['opll_mode']<<8)},")
  d.insert(1, "NULL," if len(chs["@"].keys())==0 else f"{name}_sound,")
  print(f"u8* const {name}[]={{{''.join(d)}}};")
  G.all_len += 2*4
  print(f"data size {G.all_len}bytes.",file=sys.stderr)

def main():
  print("/*")
  str = read_all(sys.argv[1])
  mml_compile(sys.argv[2],loop_expand(parse(str)))

main()
