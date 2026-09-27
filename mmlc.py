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
PTONEL="PTONEL"
PKEYOFFL="PKEYOFFL"
PLFO="PLFO"
PLFOOFF="PLFOOFF"
PPORTA="PPORTA"
PDRUMV="PDRUMV"
PDRUMV1="PDRUMV1"
PDRUMV2="PDRUMV2"
PNEXTS="PNEXTS"
PBREAKS="PBREAKS"
# ドライバの音程表と同じ F-Number (o4a = 290)
TONES=[172,182,194,205,217,230,244,258,273,290,307,325]
# 細かいデチューン @\n で使う、1 つ上の半音の F-Number (MGSDRV と同じ。b の上は 342)
NEXT_TONES=[182,194,205,217,230,244,258,273,290,307,325,342]
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
  
ENVS={} # ソフトウェアエンベロープ @e (番号 → 1 フレームごとの [音色の変更, 音量 0〜15] の並び)
def parse_env(n,src):
  """@e8={,,@13ffe@7fed} のデータを読む。16 進 1 文字が 1 フレームの音量 (f が v の音量)、@n は音色の変更 (フレームを使わない)"""
  prm=src.split(",")
  if len(prm)!=3 or prm[0] or prm[1]: fail(f"@e{n} = {{{src}}}: {{,,データ}} の形だけ使える (前の 2 つの数は未対応)")
  r=[]; at=None; d=prm[2]; i=0
  while i<len(d):
    m=re.match("@([0-9]+)",d[i:])
    if m:
      if int(m[1])>=15: fail(f"@e{n}: エンベロープの中の音色 @{m[1]} は内蔵音色 (@0〜@14) だけ使える")
      at=int(m[1]); i+=len(m[0]); continue
    if d[i] not in "0123456789abcdefABCDEF": fail(f"@e{n}: エンベロープのデータ '{d[i]}' を解釈できない")
    r.append((at,int(d[i],16))); at=None; i+=1
  if not r: fail(f"@e{n}: エンベロープのデータがない")
  ENVS[n]=r
def parse_at(lines):
  r = {};m=[]
  for l in lines:
    if ptn("^e([0-9]+)=\\{([^\\}]*)\\}$",l,m): parse_env(int(m[1]),m[2]); continue
    # @v17={...} と @17={...} は同じ (MGSDRV と同じ)
    if ptn("^v?([0-9]+)=\\{([^\\}]+)\\}$",l,m):
      r["@"+m[1]]=conv_voice(list(map(int,m[2].split(","))))
  return r
def parse_sharp(lines):
  r = {"opll_mode":0}; m=[]
  for l in lines:
    if ptn("^(opll_mode|tempo)\s+([0-9]+)",l,m): r[m[1]]=int(m[2])
    elif ptn("^(title)\s*{\s*\"([^\"]+)\"\s*}",l,m): r[m[1]]=m[2]
  return r
def parse_channel(ch,src,drum):
  r = []; l=48; pos = 0; counts = [] # [n で書いたループの回数
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
      # 長さがなくて付点があれば既定の長さに付ける (MGSDRV と同じ。l8 の c. は c8.)
      if l == None and src[pos]=="." and default!=Exception: l = default
      if l == None: return None
      d = l
      while src[pos]==".": pos+=1; d//=2; l+=d # 付点は直前に足した長さの半分 (切り捨て)
      return l
    nonlocal src,pos; spos = pos
    l = vlen2()
    # ^ は長さを足す。^ の前後に長さがなければ既定の長さを使う (MGSDRV と同じ。l8 の c^4 は c8^4、c4^ は c4^8)
    if l==None and src[pos]=="^" and default!=Exception: l = default
    if l==None:
      if default!=Exception: return default
      fail_at(ch,pos,"長さがない")
    while src[pos]=="^":
      pos+=1
      l2 = vlen2()
      if l2==None:
        if default==Exception: fail_at(ch,pos,"^ のあとに長さがない")
        l2 = default
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
        if src[pos]=="_" and c!="r":
          # ポルタメント c_e4: 始めの音には長さを書かない。_ のあとの < > はふつうのオクターブ変更
          pos+=1; o("tone_p",c); continue
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
      case "[": o(c); counts.append(readInt(None)) # [3 のように先頭にも回数を書ける
      case "]":
        # 回数は [n があればそれ (]m より優先)、なければ ]m、どちらもなければ 2 回 (MGSDRV と同じ)
        n=readInt(None); n0=counts.pop() if counts else None
        o(c, n0 if n0 is not None else n if n is not None else 2)
      case "@" if src[pos]=="e": pos+=1; o("@e",readInt()) # ソフトウェアエンベロープ。@e0 で止める
      case "@" if src[pos]=="\\": # 細かいデチューン @\n (0〜255。255 で半音)
        pos+=1; n=readInt()
        if not 0<=n<=255: fail_at(ch,pos-1,f"@\\ の値 {n} は 0〜255")
        o("@\\",n)
      case "@" | "o" | "v" | "q" | "t": o(c,readInt())
      case "y": # レジスタに直接書く y レジスタ,値
        rg=readInt()
        if src[pos]!=",": fail_at(ch,pos,"y は y レジスタ,値 の 2 つの数で書く")
        pos+=1; o("y",rg,readInt())
      case "h": # ソフトウェア LFO。h 遅れ,振れ幅の段数,速さ,1 段の値。hf で止め、ho で動かす
        if src[pos]=="f": pos+=1; o("hf")
        elif src[pos]=="o": pos+=1; o("ho")
        else:
          vals=[readInt()]
          for _ in range(3):
            if src[pos]!=",": fail_at(ch,pos,"h は h 遅れ,振れ幅,速さ,値 の 4 つの数で書く")
            pos+=1; vals.append(readInt())
          o("h",*vals)
      case "s":
        if src[pos]=="o": pos+=1; o("so")
        elif src[pos]=="f": pos+=1; o("sf")
      case "<" | ">" | "|": o(c)
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
    G.volume=15; G.octave=4; G.at=None; G.dv={}; G.dt=(0,0); G.lf=(False,None) # G.dt は (\, @\)
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
          case ["\\",n]: G.dt=(n,G.dt[1])
          case ["@\\",n]: G.dt=(G.dt[0],n)
          case ["h",*prm]: G.lf=(prm[3]!=0,tuple(prm))
          case ["hf"]: G.lf=(False,G.lf[1])
          case ["ho"]: G.lf=(G.lf[1] is not None and G.lf[1][3]!=0,G.lf[1])
          case ["drum_v",a,"",n]: G.dv=dict(G.dv); G.dv[a]=int(n)
          case ["drum_v",a,sign,n]: G.dv=dict(G.dv); G.dv[a]=G.dv.get(a,0)+(int(n) if sign=="+" else -int(n))
          case ["<"] if 0<G.octave: G.octave-=1
          case [">"] if G.octave<7: G.octave+=1
          case ["["]: stack.append([len(r),None,G.volume,G.octave,None,G.at,G.dv,G.dt,G.lf])
          case ["|"]:
            if not stack: fail(f"チャンネル {name}: | がループの外にある")
            stack[-1][1]=len(r); stack[-1][4]=(G.volume,G.octave,G.at,G.dv,G.dt,G.lf)
          case ["]",_] if not stack: fail(f"チャンネル {name}: ] に対応する [ がない")
          case ["]",n]:
            [start,br,vol,octave,brstate,at,dv,dt,lf]= stack.pop()
            if br == None: br=len(r)
            # オクターブに依存するループか: 本体で o より前に音符か < > がある
            use_octave = False
            for c in r[start+1:]:
              if c[0] == "o": break
              if c[0] in ("<",">","tone_p") or (c[0] == "tone" and c[1] != "r"): use_octave = True; break
            # 音色に依存するループか: 本体で @ より前に音符がある
            use_at = False
            for c in r[start+1:]:
              if c[0] == "@": break
              if c[0] == "tone" and c[1] != "r": use_at = True; break
            # デチューンに依存するループか: 本体で \ か @\ より前に音符がある
            use_dt = False
            for c in r[start+1:]:
              if c[0] in ("\\","@\\"): break
              if c[0] == "tone" and c[1] != "r": use_dt = True; break
            # LFO に依存するループか: 本体で h・hf・ho より前に音符か休符がある (音符とキーオフの命令が変わる)
            use_lf = False
            for c in r[start+1:]:
              if c[0] in ("h","hf","ho"): break
              if c[0] == "tone": use_lf = True; break
            # ドラムの楽器ごとの相対音量 (vs+1 など) で 1 周ごとに音量が変わるループも展開する
            if (G.volume != vol or (use_octave and G.octave != octave) or (use_at and G.at != at) or (use_dt and G.dt != dt) or (use_lf and G.lf != lf) or G.dv != dv) and n!=0: # 状態が違うので展開する
              # 展開したあとのオクターブ: 最後の周の | (なければ終わり) の時点の値。
              # オクターブに依存するループなら、1 周の変化 x (n-1) が積み重なる
              # (音量は今までどおり 1 周した後の値のまま。直すと展開が増えるので別に考える)
              bo = brstate[1] if brstate else G.octave
              G.octave = bo+(G.octave-octave)*(n-1) if use_octave else bo
              if brstate: G.at = brstate[2]; G.dt = brstate[4]; G.lf = brstate[5]
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
            if brstate: G.volume,G.octave,G.at,G.dv,G.dt,G.lf = brstate
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
        case ["\\",n]: st["dt"]=(n,st["dt"][1])
        case ["@\\",n]: st["dt"]=(st["dt"][0],n)
        case ["h",*prm]: st["lf"]=(prm[3]!=0,tuple(prm))
        case ["hf"]: st["lf"]=(False,st["lf"][1])
        case ["ho"]: st["lf"]=(st["lf"][1] is not None and st["lf"][1][3]!=0,st["lf"][1])
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
    if uses("\\",("@\\",)) or uses("@\\",("\\",)): keys.append("dt")
    if uses("h",("hf","ho")) or uses("hf",("h","ho")) or uses("ho",("h","hf")): keys.append("lf")
  if not keys: return tokens
  key=lambda st: tuple(str(st[k]) for k in keys)
  st=loop_state(tokens[:start], {"o":4,"v":15,"at":None,"dt":(0,0),"lf":(False,None),"rv":15,"dv":{k:15 for k in "bsmch"}}, drum)
  k=0; s0=st
  while True:
    nx=loop_state(body, st, drum)
    if key(nx)==key(st): break
    k+=1; st=nx
    if k>16:
      print(f"warning: {name} 無限ループの状態が 16 周で落ち着かないので展開しない",file=sys.stderr)
      return tokens
  if k==0: return tokens
  names={"o":"オクターブ","v":"音量","at":"音色","rv":"ドラムの基準の音量","dv":"ドラムの音量","dt":"デチューン","lf":"LFO"}
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
    G.volume=0; G.stack = []; G.stackMax = 0; G.o=4; G.slar=False; G.detune=0; G.fine=0 # \ と @\
    G.lfo=None; G.lfo_on=False # LFO の値 (h の 4 つ) と、動かしているか
    G.porta=None # ポルタメントの始めの音程 (ブロック, F-Number)
    G.env=None; G.env_q=[] # ソフトウェアエンベロープと、まだ出していないフレームの 0x30 の値
    G.lpitch=None # 直前の LFO をかけた音 (PTONEL) の音程
    G.legato=False # 直前が & (キーオンしない)
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
    # 全音符のフレーム数。MGSDRV は 60*60*4/テンポ を切り捨てた整数にしてから音符に分けるので同じにする
    # (テンポ 112 は 128.57 ではなく 128 フレーム。書いたテンポより少し速くなる)
    G.t = 60*60*4//(chs["#"]["tempo"] if "tempo" in chs["#"] else 120)
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
      # ソフトウェアエンベロープ: 待ちを 1 フレームずつに分けて、フレームごとの音量を出す (次のキーオンまで続く)
      while G.env_q and n>0:
        p2(1); n-=1; fk=k=PWAIT
        v=G.env_q.pop(0)
        if v!=G.old_volume: p(PVOLUME,v); G.old_volume=v
      while n>=256: p2(0);n-=256
      if n!=0: p2(n)
    def pitch(b):
      # 音 (0〜11) の音程 (ブロック, F-Number)。デチューンを足して、172〜344 から出たらブロックをまたぐ
      # @\n は 1 つ上の半音との差の (n+1)/256 を足す (MGSDRV と同じ)
      n=b+G.o*12; t=n%12; blk=n//12
      f=TONES[t]+((NEXT_TONES[t]-TONES[t])*(G.fine+1)>>8)+G.detune
      while f<172: f+=173; blk-=1
      while f>=345: f-=173; blk+=1
      return blk&7,f
    def cmd_compile(name,v):
      nonlocal vi
      match v:
        case ["tone","r",a]:
                      # エンベロープを使っている間は、音量はエンベロープの値のまま (最後の値で止まる)
                      if not G.env and not G.env_q: outvolume()
                      # 休符でキーオフする (MGSDRV と同じ)。リズムモードの ch6〜8 は
                      # 0x26〜0x28 がリズムの音程なので書かない
                      if chs["#"]["opll_mode"] and i >= 6: outwait("r",PWAIT,PWAIT,a/192)
                      else: outwait("r",PKEYOFFL if G.lfo_on else PKEYOFF,PWAIT,a/192)
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
                      legato=G.legato; G.legato=False # スラーでつなぐ音はエンベロープをやり直さない
                      if G.env and not legato:
                        # キーオンでエンベロープをやり直す。最初のフレームの値はキーオンと同じフレームに出す
                        at=G.at; G.env_q=[]
                        for eat,x in G.env:
                          if eat is not None: at=eat+1
                          G.env_q.append((at<<4)|min(15,G.volume+15-x))
                        v=G.env_q.pop(0)
                        if v!=G.old_volume: p(PVOLUME,v); G.old_volume=v
                      else:
                        if not legato: G.env_q=[]
                        if not G.env and not G.env_q: outvolume()
                      porta=G.porta; G.porta=None; pi=None; tie=False
                      if porta:
                        # ポルタメント: 始めの音程から N フレームかけてこの音へ。1 フレームに |差|/N ずつ (余りは dda)
                        blk,f=pitch(b)
                        dl=(blk*173+f)-(porta[0]*173+porta[1])
                        pi=len(G.r)+3
                        p(PPORTA,porta[1]&255,(porta[0]<<1)|(porta[1]>>8),0,0,0,1 if dl<0 else 0)
                        all0=G.all
                      elif G.lfo_on and legato and G.lpitch==pitch(b):
                        # LFO をかけた音を同じ音程でスラーでつなぐときは、MGSDRV は何もしない (LFO も続ける)。
                        # 直前の PSLAON を消して、音を出し直さずに待つだけにする
                        del G.r[len(G.r)-1-G.r[::-1].index(PSLAON)]
                        tie=True
                      elif G.lfo_on or pitch(b)!=((b+G.o*12)//12,TONES[b%12]):
                        # デチューンで音程表と違う音は、音程をデータで持つ (PTONEF)。172〜344 から出たらブロックをまたぐ
                        # LFO をかける音も音程をデータで持つ (PTONEL)
                        blk,f=pitch(b)
                        p(PTONEL if G.lfo_on else PTONEF,f&255,(blk<<1)|(f>>8))
                      else: p(f"/*PTONE,*/{b+G.o*12}")
                      G.lpitch=pitch(b) if G.lfo_on and not porta else None
                      # スラー & でつなぐ音は q で詰めずに最後まで鳴らす (MGSDRV と同じ)
                      q = 1 if vi < len(ch) and ch[vi][0] == "&" else G.q
                      outwait(f"tone {b}", PWAIT if tie else False,PWAIT,w*q)
                      ko = PKEYOFFL if G.lfo_on or porta else PKEYOFF # LFO・ポルタメントの音はキーの状態を覚える
                      if q!=1: outwait(f"off {b}",ko,ko,w*(1-q))
                      if porta:
                        nn=G.all-all0; qq,rr=divmod(abs(dl),nn) if nn else (0,0)
                        if not 1<=nn<=255: fail(f"チャンネル {name}: ポルタメントの音の長さ {nn} フレームは 1〜255 でないといけない")
                        if qq>255: fail(f"チャンネル {name}: ポルタメントの音程の差が大きすぎる (1 フレームに {qq})")
                        G.r[pi],G.r[pi+1],G.r[pi+2]=f"{qq}",f"{rr}",f"{nn}"
        case ["tone_p",b]:
                      notes={"c":0,"c+":1,"d":2,"d+":3,"e-":3,"e":4,"f":5,"f+":6,"g":7,"g+":8,"a":9,"a+":10,"b-":10,"b":11}
                      G.porta=pitch(notes[b])
        case ["l",l]: G.l=l
        case ["q",q]: G.q=q/8
        case ["o",o]: G.o=o-1
        case [">"]:
                      if G.o<7:G.o+=1
        case ["<"]:
                      if G.o>0:G.o-=1
        case ["t",t]: G.t=60*60*4//t; G.tempos[int(G.all2*192)]=G.t; print(f"t {G.all2*192}",file=sys.stderr)
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
                        elif ch[j][0] in ("@","v","v-","v+","@e"): G.old_volume=-1
                        j+=1
                      if G.env: G.old_volume=-1 # エンベロープで音量が変わるので、2 周目の入口の音量はわからない
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
                      # dda: 1 周ごとに diff1 を足し、n1 以上になったら 1 フレーム待つ (n1 周で diff1 フレーム)。
                      # 無限ループ (n1=0) は 1 周を 1 回と数えるので 1。0 だと毎周 1 フレーム余計に待ってしまう
                      p(diff1,n1 if n1 else 1)
                      print(f"  [ {al2-al} ]{n1} {diff1}",file=sys.stderr)
                      #outwait(f"]{n+1+int(bool(br))}",PWAIT,PWAIT,0)
                      if len(G.stack) == 0 and n1 == 0: G.intro = al
                      if br: # ブレイクアドレス
                        # 最後の周は | で抜けるので、ループのあとは | の時点の状態になる
                        G.o,G.volume,G.old_volume,G.at,G.q,G.detune,G.fine,G.lfo,G.lfo_on = brstate
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
                      G.stack[-1][7]=(G.o,G.volume,G.old_volume,G.at,G.q,G.detune,G.fine,G.lfo,G.lfo_on)
                      p(PBREAK,None,None)
        case ["drum",v,w]: w=w/192;out_drum_volume(v);p(f"/*PDRUM*/{v+0x60}");outwait(f"drum {v}",None,PWAIT,w)
        case ["drum_v",a,"+",n]: G.drum_v[a]=min(15,G.drum_v[a]+int(n)) # 0〜15 に収める (MGSDRV と同じ)
        case ["drum_v",a,"-",n]: G.drum_v[a]=max(0,G.drum_v[a]-int(n))
        case ["drum_v",a,"",n]: G.drum_v[a]=int(n); G.drum_rv=int(n)
        case ["&"]: p(PSLAON); G.legato=True
        case ["@e",n]:
                      if n==0: G.env=None
                      elif n not in ENVS: fail(f"チャンネル {name}: エンベロープ @e{n} が定義されていない (@e{n} = {{,,...}})")
                      else: G.env=ENVS[n]
        case ["so"]: p(PSUSON)
        case ["sf"]: p(PSUSOFF)
        case ["h",a,b,c,d]:
                      G.lfo=(a,b,c,d)
                      if d: p(PLFO,a&255,b&255,c&255,d&255); G.lfo_on=True
                      else: p(PLFOOFF); G.lfo_on=False # 1 段の値が 0 なら動かさない
        case ["hf"]:  p(PLFOOFF); G.lfo_on=False
        case ["ho"]:
                      if G.lfo and G.lfo[3]: a,b,c,d=G.lfo; p(PLFO,a&255,b&255,c&255,d&255); G.lfo_on=True
        case ["\\",n]: G.detune=n
        case ["@\\",n]: G.fine=n
        case ["y",rg,v]:
                      p(PDRUMV,rg&255,v&255) # PDRUMV は任意のレジスタに書く命令
                      G.old_volume=-1 # 音量のレジスタを書き換えたかもしれないので、次の音で出し直す (MGSDRV もキーオンで書き直す)
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
