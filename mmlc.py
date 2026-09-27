import re,sys,math

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
PSUSOFF="PSUSOFF"
PTONEF="PTONEF"
PDRUMV="PDRUMV"
PDRUMV1="PDRUMV1"
PDRUMV2="PDRUMV2"
PNEXTS="PNEXTS"
PBREAKS="PBREAKS"
# ドライバの音程表と同じ F-Number (o4a = 290)
TONES=[172,182,194,205,217,230,244,258,273,290,307,325]
# エラーは「ファイル名:行: error: 内容」を標準エラーに出して止める (main で受ける)
class MmlError(Exception): pass
FILENAME="-"
LINEMAP={} # チャンネルごとに、つないだ文字列の位置 → (元の行番号, その行の文字列)
def fail(msg,line=None,text=None,col=None):
  where=f"{FILENAME}:{line}" if line else FILENAME
  s=f"{where}: error: {msg}"
  if text is not None:
    s+=f"\n  {text}"
    if col is not None: s+="\n  "+" "*col+"^"
  raise MmlError(s)
def fail_at(ch,pos,msg):
  """チャンネル ch をつないだ文字列の pos の位置でエラーにする。元の行番号とその行を出す"""
  for start,line,text,orig in reversed(LINEMAP.get(ch,[])):
    if start<=pos:
      # マクロを展開した行は、元の行と展開したあとの文字列を両方出す
      if text!=orig.split(None,1)[-1].replace(" ",""): fail(f"チャンネル {ch}: {msg}",line,f"{orig}\n  -> {text}",pos-start+3)
      fail(f"チャンネル {ch}: {msg}",line,text,pos-start)
  fail(f"チャンネル {ch}: {msg}")
def ptn(p,s,m):
  v = re.match(p,s)
  if v==None: m[:]=[""]; return False
  r = [v.group()]; r.extend(v.groups()); m[:]=r
  return True

def preprocess(src):
    pos = 0; m=[]
    macro={}; macrows={}
    r = {"@":[],"#":[],"9":[],"A":[],"B":[],"C":[],"D":[],"E":[],"F":[],"G":[],"H":[]}; ch = 0
    lines = {k:[] for k in r} # r と同じ並びで、その行の行番号
    lineno = lambda: src.count("\n",0,pos)+1
    def pt(pt,m):
      nonlocal pos,src
      if ptn(pt,src[pos:],m): pos+=len(m[0]); return True
      return False
    def o(ch,*data): nonlocal r; r[ch].extend(data)
    while pos < len(src):
      if pt("^[ \t\r\n]+",m): pass
      elif pt("^;[^\r\n]*",m): pass
      elif pt("^#([^;\r\n]+)",m):
        o("#",m[1]); m1=[]
        # #macro_offset はそれより後ろの行にだけ効く
        if ptn("^macro_offset\s*\\{([^}]+)\\}",m[1],m1):
          for wn in re.split(",",m1[1]):
            kv=wn.replace(" ","").split("=")
            if len(kv)!=2 or not re.match("^[a-zA-Z]$",kv[0]) or not re.match("^-?[0-9]+$",kv[1]):
              fail(f"macro_offset の書き方が違う: {wn.strip()}",lineno(),"#"+m[1].strip())
            macrows[kv[0]]=int(kv[1])
      elif pt("^(\\*[0-9]+)\s*=\s*\{([^}]*)\}",m): print(f"macro {m[1]}");macro[m[1]]=m[2].replace(" ","")
      elif pt("^@((;[^\r\n]+[\r\n]*|[^;\r\n}]+|[\r\n]+)+\})",m):
        n=m[1];r2=[];m1=[]
        while len(n)>0:
            if ptn("^;[^\r\n]+[\r\n]*|[\r\n]+|\s+",n,m1): n=n[len(m1[0]):]; continue
            if ptn("^[^;\r\n\s]+",n,m1): r2.append(m1[0]);n=n[len(m1[0]):]; continue
            print(f"error {m}")
        o("@","".join(r2))
      elif pt("^([^\s]+)[ \t]+([^;\r\n]+)",m):
        ln=src.count("\n",0,pos-len(m[0]))+1; text=m[0].rstrip()
        # *h1 などはこの行の時点の macro_offset で *5 のような番号に直す
        def macw(w):
          if w.group(1) not in macrows: fail(f"*{w.group(1)} の macro_offset がない",ln,text)
          return f"*{macrows[w.group(1)]+(int(w.group(2)) if w.group(2) else 0)}"
        v=re.compile("\\*([a-zA-Z])([0-9]*)").sub(macw,m[2].replace(" ",""))
        for x in m[1]:
          if x not in r or x in "@#": fail(f"チャンネル {x} は使えない (9・A〜H)",ln,text)
          o(x,v); lines[x].append((ln,text))
      else: pos+=1
    for k,vs in r.items():
      if k=="@" or k == "#": continue
      start=0; LINEMAP[k]=[]
      for i,v in enumerate(vs):
        def macf(w):
          if w.group(0) not in macro: fail(f"マクロ {w.group(0)} が定義されていない",*lines[k][i])
          return macro[w.group(0)]
        r[k][i]=re.compile("\\*([0-9]+)").sub(macf,v)
        # エラーの位置は、マクロを展開したあとの文字列で数える
        LINEMAP[k].append((start,lines[k][i][0],r[k][i],lines[k][i][1])); start+=len(r[k][i])
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
    if default==Exception: fail_at(ch,pos,f"数がない ('{src[pos-1]}' のあと)")
    return default
  def readLen(c,default=Exception):
    def vlen():
      nonlocal src,pos; r=[]
      if ptn("^[0-9]+",src[pos:],r):
          # 長さは整数の tick (全音符 = 192) に切り捨てる (MGSDRV と同じ。c129 は 1 tick)
          pos += len(r[0]); return 0 if r[0]=="0" else 192//int(r[0])
      if ptn("^%[0-9]+",src[pos:],r):pos += len(r[0]); return int(r[0][1:])
      return None
    def vlen2():
      nonlocal src,pos
      l = vlen()
      if l == None: return None
      d = l
      while src[pos]==".": pos+=1; d//=2; l+=d # 付点は直前に足した長さの半分 (切り捨て)
      return l
    nonlocal src,pos; spos = pos
    l = vlen2()
    if l==None:
      if default!=Exception: return default
      fail_at(ch,pos,"長さがない")
    while src[pos]=="^":
      pos+=1
      if src[pos] == c: pos+=1
      l2 = vlen2()
      if l2==None: fail_at(ch,pos,"^ のあとに長さがない")
      l += l2
    return l
  def o(*data): nonlocal r; r.append(list(data))
  def err():
    nonlocal pos,ch,src
    pos -= 1
    fail_at(ch,pos,f"'{src[pos]}' を解釈できない")
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
      case "[" | "<" | ">" | "|": o(c)
      # & はすぐ前が音符のときだけ効く (MGSDRV と同じ。「c8(&d8」や「[c8|&]2」ではつながない)
      case "&" if r and r[-1][0] == "tone": o(c)
      case "&": pass
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
    at=None
    before=0
    after=0
  def expand(n,ch):
    name=n # n はループ回数などで上書きされるので、チャンネル名は別に持つ
    G.before+=len(ch)
    G.volume=15; G.octave=4; G.at=None; G.dv={}; G.dt=0
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
          case ["@",n]: G.at=n
          case ["\\",n]: G.dt=n
          case ["drum_v",a,"",n]: G.dv=dict(G.dv); G.dv[a]=int(n)
          case ["drum_v",a,sign,n]: G.dv=dict(G.dv); G.dv[a]=G.dv.get(a,0)+(int(n) if sign=="+" else -int(n))
          case ["<"] if 0<G.octave: G.octave-=1
          case [">"] if G.octave<7: G.octave+=1
          case ["["]: stack.append([len(r),None,G.volume,G.octave,None,G.at,G.dv,G.dt])
          case ["|"]:
            if not stack: fail(f"チャンネル {name}: | がループの外にある")
            stack[-1][1]=len(r); stack[-1][4]=(G.volume,G.octave,G.at,G.dv,G.dt)
          case ["]",_] if not stack: fail(f"チャンネル {name}: ] に対応する [ がない")
          case ["]",n]:
            [start,br,vol,octave,brstate,at,dv,dt]= stack.pop()
            if br == None: br=len(r)
            # オクターブに依存するループか: 本体で o より前に音符か < > がある
            use_octave = False
            for c in r[start+1:]:
              if c[0] == "o": break
              if c[0] in ("<",">") or (c[0] == "tone" and c[1] != "r"): use_octave = True; break
            # 音色に依存するループか: 本体で @ より前に音符がある
            use_at = False
            for c in r[start+1:]:
              if c[0] == "@": break
              if c[0] == "tone" and c[1] != "r": use_at = True; break
            # デチューンに依存するループか: 本体で \ より前に音符がある
            use_dt = False
            for c in r[start+1:]:
              if c[0] == "\\": break
              if c[0] == "tone" and c[1] != "r": use_dt = True; break
            # ドラムの楽器ごとの相対音量 (vs+1 など) で 1 周ごとに音量が変わるループも展開する
            if (G.volume != vol or (use_octave and G.octave != octave) or (use_at and G.at != at) or (use_dt and G.dt != dt) or G.dv != dv) and n!=0: # 状態が違うので展開する
              # 展開したあとのオクターブ: 最後の周の | (なければ終わり) の時点の値。
              # オクターブに依存するループなら、1 周の変化 x (n-1) が積み重なる
              # (音量は今までどおり 1 周した後の値のまま。直すと展開が増えるので別に考える)
              bo = brstate[1] if brstate else G.octave
              G.octave = bo+(G.octave-octave)*(n-1) if use_octave else bo
              if brstate: G.at = brstate[2]; G.dt = brstate[4]
              G.dv = {k: G.dv[k]+(G.dv[k]-dv.get(k,0))*(n-1) for k in G.dv} # 相対の変化は n 周分
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
            # 展開しないループで | があれば、ループのあとは | の時点の状態になる
            if brstate: G.volume,G.octave,G.at,G.dv,G.dt = brstate
      r.append(v)
    return r
  r = {}
  for n,ch in chs.items():
    if n=="@" or n=="#" or ch==None: r[n]=ch; continue
    r[n]= expand(n,ch)
    r[n]= unroll_infinite(n,r[n],chs["#"]["opll_mode"] and n=="F")
  print(f"loop expand {G.before}+{G.after} to {G.before+G.after}commands +{G.after/G.before*100:0.2f}%",file=sys.stderr)
  return r

def loop_state(tokens, st, drum):
  """tokens を鳴らしたあとの状態 (オクターブ・音量・音色・ドラム音量) を返す。ループは回数分まわす"""
  st = dict(st, dv=dict(st["dv"]))
  def run(i, end):
    while i < end:
      c = tokens[i]
      match c:
        case ["o",n]: st["o"]=n-1
        case ["<"] if st["o"]>0: st["o"]-=1
        case [">"] if st["o"]<7: st["o"]+=1
        case ["@",n]: st["at"]=n
        case ["\\",n]: st["dt"]=n
        case ["v",n] if drum: st["rv"]=n; st["dv"]={k:n for k in st["dv"]}
        case ["v",n]: st["v"]=n
        case ["v-"|"v+",n] if drum: st["rv"]=min(15,max(0,st["rv"]+n)); st["dv"]={k:st["rv"] for k in st["dv"]}
        case ["v-"|"v+",n]: st["v"]=min(15,max(0,st["v"]+n))
        case ["drum_v",a,"",n]: st["dv"][a]=int(n); st["rv"]=int(n)
        case ["drum_v",a,"+",n]: st["dv"][a]=min(15,st["dv"][a]+int(n))
        case ["drum_v",a,"-",n]: st["dv"][a]=max(0,st["dv"][a]-int(n))
        case ["["]:
          # 対応する ] と | を探して、回数分まわす
          d=0; br=None; j=i
          while True:
            j+=1
            if tokens[j][0]=="[": d+=1
            elif tokens[j][0]=="]":
              if d==0: break
              d-=1
            elif tokens[j][0]=="|" and d==0: br=j
          cnt=max(tokens[j][1],1)
          for k in range(cnt):
            if br is not None and k==cnt-1: run(i+1,br); break
            run(i+1,j)
          i=j
      i+=1
  run(0, len(tokens))
  return st

def unroll_infinite(name, tokens, drum):
  """一番外側の無限ループ [本体]0 で、1 周すると状態 (オクターブ・音量・音色・ドラム音量) が変わるなら、
  状態が変わらなくなるまで本体を並べてから無限ループにする: 本体(S0) 本体(S1) ... [本体(Sk)]0
  MGSDRV は 2 周目以降を変わった状態のまま鳴らすが、コンパイラは本体を 1 回しかコンパイルしないため"""
  # 一番外側の [ ... ]0 を探す
  d=0; start=None
  for i,c in enumerate(tokens):
    if c[0]=="[":
      if d==0: start=i
      d+=1
    elif c[0]=="]":
      d-=1
      if d==0 and c[1]==0: end=i; break
    elif c[0]=="|" and d==1: return tokens # 無限ループのブレイクは扱わない
  else:
    return tokens
  body=tokens[start+1:end]
  # 本体が入口の状態を使うか: 状態を指定し直すより前に、その状態を使う命令があるか
  def uses(setk, usek):
    for c in body:
      if c[0]==setk: return False
      if c[0] in usek or (c[0]=="tone" and c[1]!="r"): return True
    return False
  keys=[]
  if uses("o",("<",">")): keys.append("o")
  if drum:
    if uses("v",("drum","v-","v+","drum_v")): keys+=["rv","dv"]
  else:
    if uses("v",("v-","v+")): keys.append("v")
    if uses("@",()): keys.append("at")
    if uses("\\",()): keys.append("dt")
  if not keys: return tokens
  key=lambda st: tuple(str(st[k]) for k in keys)
  st=loop_state(tokens[:start], {"o":4,"v":15,"at":None,"dt":0,"rv":15,"dv":{k:15 for k in "bsmch"}}, drum)
  k=0; s0=st
  while True:
    nx=loop_state(body, st, drum)
    if key(nx)==key(st): break
    k+=1; st=nx
    if k>16:
      print(f"warning: {name} 無限ループの状態が 16 周で落ち着かないので展開しない",file=sys.stderr)
      return tokens
  if k==0: return tokens
  names={"o":"オクターブ","v":"音量","at":"音色","rv":"ドラムの基準の音量","dv":"ドラムの音量","dt":"デチューン"}
  changed=[k for k in keys if str(s0[k])!=str(loop_state(body, s0, drum)[k])]
  print(f"warning: {name} の無限ループは 1 周で {'・'.join(names[k] for k in changed if k!='dv' or 'rv' not in changed)} が変わるので、"
        f"MGSDRV と同じに鳴らすため本体 ({len(body)} コマンド) を {k} 周分展開した (データが大きくなる)",file=sys.stderr)
  return tokens[:start]+body*k+tokens[start:]

def mml_compile(name,chs,loops=2):
  print(chs)
  print("*/")
  class G:pass
  G.tempos={}; G.all_len = 0; G.sounds={}; G.n2i={}; G.i2n={}; G.frames=0
  if len(chs["@"].keys())>0:
    ch = []; i = 0
    for k,ss in chs["@"].items(): ch.extend(map(str,ss));G.sounds[k]=i;i+=1
    print(f"u8 const {name}_sound[{len(ch)}]={{{','.join(ch)}}};")
  # 末尾の使っていないチャンネルは出力しない。出力すると終了処理 (PEND) がレジスタ 0x20+ch に 0 を書き、
  # リズムモードでは G・H (0x27・0x28) のリズムの音程が変わってしまう。途中の空きチャンネルは、
  # チャンネル番号がずれるので残す
  names = [n for n in chs if n not in "@#"]
  while names and not chs[names[-1]]: names.pop()
  i = -1
  for n,ch in chs.items():
    if n=="@" or n=="#" or n not in names: continue
    i+=1
    G.n2i[n]=i
    G.i2n[i]=n
    G.old_volume=15; G.r = []; G.at = 1
    G.volume=0; G.stack = []; G.stackMax = 0; G.o=4; G.slar=False; G.detune=0
    G.intro = None # 一番外側の無限ループ [ ]0 の前の長さ (1/60秒単位)
    G.old_drum_v=[255,255,255]; G.drum_v={"b":15,"s":15,"m":15,"c":15,"h":15}
    G.drum_rv=15 # リズムの ( ) の基準になる音量。v と vb などで最後に指定した値 (MGSDRV と同じ)
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
      # v は鳴らす楽器のフラグ (b=0x10 s=0x08 m=0x04 c=0x02 h=0x01)。鳴らす楽器のレジスタだけ書く
      w0 = (v & 0x10) and v0 != G.old_drum_v[0]
      w1 = (v & 0x09) and v1 != G.old_drum_v[1]
      w2 = (v & 0x06) and v2 != G.old_drum_v[2]
      if w0: p(PDRUMV,0x36,v0); G.old_drum_v[0]=v0
      # 0x37 と 0x38 は、よく使う形を短い命令にする (書くレジスタと順番は同じ)
      if w1 and w2 and v1 == v2: p(PDRUMV2,v1)
      elif w1: p(PDRUMV1,v1)
      if w2 and not (w1 and v1 == v2): p(PDRUMV,0x38,v2)
      if w1: G.old_drum_v[1]=v1
      if w2: G.old_drum_v[2]=v2
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
                      # 休符でキーオフする (MGSDRV と同じ)。リズムモードの ch6〜8 は
                      # 0x26〜0x28 がリズムの音程なので書かない
                      if chs["#"]["opll_mode"] and i >= 6: outwait("r",PWAIT,PWAIT,a/192)
                      else: outwait("r",PKEYOFF,PWAIT,a/192)
        case ["v",b] if name=="F" and chs["#"]["opll_mode"]: # リズムモードの F はドラムの音量
                      for k in G.drum_v.keys(): G.drum_v[k]=b
                      G.drum_rv=b
        case ["v-"|"v+",v] if name=="F" and chs["#"]["opll_mode"]:
                      # リズムの ( ) は基準の音量を変えて、全部の楽器をその音量にする
                      G.drum_rv=min(15,max(0,G.drum_rv+v))
                      for k in G.drum_v.keys(): G.drum_v[k]=G.drum_rv
        case ["v",b]: G.volume=(15-b)
        case ["tone",b,w]:
                      notes={"c":0,"c+":1,"d":2,"d+":3,"e-":3,"e":4,"f":5,"f+":6,"g":7,"g+":8,"a":9,"a+":10,"b-":10,"b":11,"r":12}
                      b=notes[b];w = w/192
                      #print(f"w {w} q {G.q}")
                      outvolume()
                      if G.detune:
                        # デチューン (MGSDRV と同じ)。F-Number に足して、172〜344 から出たらブロックをまたぐ
                        n=b+G.o*12; f=TONES[n%12]+G.detune; blk=n//12
                        while f<172: f+=173; blk-=1
                        while f>=345: f-=173; blk+=1
                        blk&=7
                        p(PTONEF,f&255,(blk<<1)|(f>>8))
                      else: p(f"/*PTONE,*/{b+G.o*12}")
                      # スラー & でつなぐ音は q で詰めずに最後まで鳴らす (MGSDRV と同じ)
                      q = 1 if vi < len(ch) and ch[vi][0] == "&" else G.q
                      outwait(f"tone {b}", False,PWAIT,w*q)
                      if q!=1: outwait(f"off {b}",PKEYOFF,PKEYOFF,w*(1-q))
        case ["l",l]: G.l=l
        case ["q",q]: G.q=q/8
        case ["o",o]: G.o=o-1
        case [">"]:
                      if G.o<7:G.o+=1
        case ["<"]:
                      if G.o>0:G.o-=1
        case ["t",t]: G.t=60*60*4/t; G.tempos[int(G.all2*192)]=G.t; print(f"t {G.all2*192}",file=sys.stderr)
        case ["@",v]  if v < 15: G.at = (v+1)
        case ["@",v]:
                      if f"@{v}" not in G.sounds: fail(f"チャンネル {name}: 音色 @{v} が定義されていない (@v{v} = {{...}})")
                      G.at=0; p(PSLOAD,G.sounds[f"@{v}"]*8)
        case ["["]:   
                      # 本体で音色や音量を変えるなら、2 周目の入口の音色・音量は 1 周目の終わりのものになるので、
                      # 直前に出した音色・音量を忘れて、本体の最初の音で必ず PVOLUME を出す
                      d=0; j=vi
                      while j < len(ch) and not (ch[j][0]=="]" and d==0):
                        if ch[j][0]=="[": d+=1
                        elif ch[j][0]=="]": d-=1
                        elif ch[j][0] in ("@","v","v-","v+"): G.old_volume=-1
                        j+=1
                      diff = max(0,G.all2-G.all) #+0.00000001
                      G.all+=diff
                      G.stack.append([len(G.r),G.all,G.all2,None,None,None,diff,None])
                      G.stackMax=max(len(G.stack),G.stackMax);p(PLOOP,0,0)
        case ["]",n]: # n回ループする
                      if not G.stack: fail(f"チャンネル {name}: ] に対応する [ がない")
                      n1=n
                      if n<2:n=1
                      [l,al,al2,br,bral,bral2,diff,brstate]=G.stack.pop();G.r[l+1]=f"{n1>>1}";G.r[l+2]=f"{n1}"
                      # ブレイクの飛び先 (PNEXT の dda の位置) が 255 バイト以内なら 1 バイトの PBREAKS にする。
                      # PNEXT を長い形 (飛び先の dda が len+3) にしたときの距離で決める。短くなれば距離も縮むので収まる
                      short_br = br is not None and len(G.r)+2-br <= 255
                      if short_br: G.r[br-1]=PBREAKS; del G.r[br+1]
                      # ループの頭へ戻る距離が -128 以上なら 1 バイトの PNEXTS にする
                      off = l-len(G.r)
                      if off >= -128: p(PNEXTS, off&255)
                      else: p(PNEXT); nn=(l-len(G.r))&0xffff; p(nn&255,nn>>8)
                      n-=1
                      if br: n-=1
                      G.all2+=(G.all2-al2)*n; G.all+=(G.all-al)*n
                      if br: G.all+=bral;G.all2+=bral2
                      G.all-=diff
                      diff=G.all2-G.all
                      diff1=int(diff)
                      G.all+=diff1
                      if len(G.stack) == 0:
                        G.all = int(G.all+0.00000001)
                        diff1 = int(diff+0.00000001)
                      p(diff1,n1)
                      print(f"  [ {al2-al} ]{n1} {diff1}",file=sys.stderr)
                      #outwait(f"]{n+1+int(bool(br))}",PWAIT,PWAIT,0)
                      if len(G.stack) == 0 and n1 == 0: G.intro = al
                      if br: # ブレイクアドレス
                        # 最後の周は | で抜けるので、ループのあとは | の時点の状態になる
                        G.o,G.volume,G.old_volume,G.at,G.q,G.detune = brstate
                        pos = len(G.r) - br - 2
                        if short_br: G.r[br]= f"{pos}"
                        else:
                          G.r[br  ]= f"{pos&255}"
                          G.r[br+1]= f"{pos>>8}"
        case ["q",q]: G.q=q
        # ( で音量を下げ、) で上げる。G.volume は減衰 (15-音量) なので逆向きに足し、0〜15 に収める
        case ["v-",v]:G.volume=min(15,max(0,G.volume-v))
        case ["v+",v]:G.volume=min(15,max(0,G.volume-v))
        case ["|"]:
                      #print("|",file=sys.stderr)
                      if not G.stack: fail(f"チャンネル {name}: | がループの外にある")
                      if G.stack[-1][3] != None: fail(f"チャンネル {name}: 1 つのループに | が 2 つある")
                      G.stack[-1][3]=len(G.r)+1
                      G.stack[-1][4]=G.all-G.stack[-1][1]
                      G.stack[-1][5]=G.all2-G.stack[-1][2]
                      G.stack[-1][7]=(G.o,G.volume,G.old_volume,G.at,G.q,G.detune)
                      p(PBREAK,None,None)
        case ["drum",v,w]: w=w/192;out_drum_volume(v);p(f"/*PDRUM*/{v+0x60}");outwait(f"drum {v}",None,PWAIT,w)
        case ["drum_v",a,"+",n]: G.drum_v[a]=min(15,G.drum_v[a]+int(n)) # 0〜15 に収める (MGSDRV と同じ)
        case ["drum_v",a,"-",n]: G.drum_v[a]=max(0,G.drum_v[a]-int(n))
        case ["drum_v",a,"",n]: G.drum_v[a]=int(n); G.drum_rv=int(n)
        case ["&"]: p(PSLAON)
        case ["so"]: p(PSUSON)
        case ["sf"]: p(PSUSOFF)
        case ["\\",n]: G.detune=n
        case v:       fail(f"チャンネル {name}: 対応していない命令 {v}")
    vi = 0
    while vi<len(ch):
      v = ch[vi]; vi += 1; cmd_compile(n,v)
    # 閉じていない [ は MGSDRV もそのまま通す (1 回だけ鳴らす) ので、警告だけ出す
    if G.stack: print(f"{FILENAME}: warning: チャンネル {n}: [ が {len(G.stack)} 個閉じていない",file=sys.stderr)
    p(PEND)
    G.r.insert(0,f"{G.stackMax}")
    split=",\n  "
    print(f"u8 const {name}_{i}[{len(G.r)}]={{\n  {split.join(G.r)}}};")
    G.all_len += len(G.r)
    print(f"{n} all {G.all} {G.all2}",file=sys.stderr)
    # 演奏時間: 無限ループならイントロ + 本体 x loops、なければ最後まで
    frames = G.all if G.intro is None else G.intro + (G.all-G.intro)*loops
    G.frames = max(G.frames, math.ceil(frames))
    
  d = list(map(lambda i:f'{name}_{i},',range(i+1)))
  if "F" in G.n2i and G.n2i["F"]!=6:
    print(f"n2i {list(G.n2i.items())}")
  d.insert(0,f"(u8*){len(d)|(chs['#']['opll_mode']<<8)},")
  d.insert(1, "NULL," if len(chs["@"].keys())==0 else f"{name}_sound,")
  print(f"u8* const {name}[]={{{''.join(d)}}};")
  print(f"#define {name}_frames {G.frames}")
  print(f"frames {G.frames} ({G.frames/60:.2f}sec.)",file=sys.stderr)
  if G.frames > 65000: fail(f"frames {G.frames} > 65000")
  G.all_len += 2*4
  print(f"data size {G.all_len}bytes.",file=sys.stderr)

def main():
  global FILENAME
  print("/*")
  FILENAME = sys.argv[1]
  str = read_all(sys.argv[1])
  loops = int(sys.argv[3]) if len(sys.argv) > 3 else 2
  try:
    mml_compile(sys.argv[2],loop_expand(parse(str)),loops)
  except MmlError as e:
    print(e,file=sys.stderr); sys.exit(1)

main()
