# FM の p_exec の命令の振り分け (cp の比較の木) を、命令の回数から求める
#
# 使い方:
#   make build OPTION="-D OPT=1 -D OPT2=1 -D OPT3=1 -D COUNT=1" SRC=ys2_02 FRAMES=3600 OPEN=true
#   (曲ごとに) → build/曲名/OPT_OPT2_OPT3_COUNT_f3600/result に "count fm 81 000012AB" の行が出る
#   python3 bin/optree.py build/ys2_02/OPT_OPT2_OPT3_COUNT_f3600/result ... [--light result ...]
# --light のあとの曲 (デモ用など) は回数を 0.1 倍して混ぜる
#
# 比較は cp (7 クロック) と jp c・jp z・jp nc (どれも 10 クロック)。命令の番号の順は変えずに、
# 区間ごとに一番安い分け方 (2 つ分け、または = を先に見る 3 つ分け) を全部試す
import sys,re,functools

names={}
for l in open('oplldrv.h'):
    m=re.match(r'#define (P[A-Z0-9]+)\s+0x([0-9A-F]+)',l)
    if m: names[int(m[2],16)]=m[1]
ITEMS=[('PTONE',0x00,0x5F),('PDRUM',0x60,0x7F)]+[(names[v],v,v) for v in range(0x80,max(names)+1) if v in names]
n=len(ITEMS)
w=[0.0]*n
scale=1.0
for a in sys.argv[1:]:
    if a=='--light': scale=0.1; continue
    for l in open(a):
        if not l.startswith('count fm '): continue
        _,_,op,v=l.split(); op=int(op,16)
        for i,(_,lo,hi) in enumerate(ITEMS):
            if lo<=op<=hi: w[i]+=int(v,16)*scale
W=lambda i,j: sum(w[i:j+1])

@functools.lru_cache(None)
def C(i,j): # 区間 i..j に上から入ってきたときのクロック (回数の重み付き) と木
    if i==j: return (10*w[i],('leaf',i)) # 通り過ぎて着いたら jp が 1 つ要る
    sub=lambda a,b: (0,('leaf',a)) if a==b else C(a,b) # jp c・jp z で直接飛ぶ先
    best=None
    for m in range(i+1,j+1): # cp 項目 m の値: 左 i..m-1 / 右 m..j
        for cost,t in ((17*W(i,j)+sub(i,m-1)[0]+C(m,j)[0],('jc',m,sub(i,m-1)[1],C(m,j)[1])),
                       (17*W(i,j)+C(i,m-1)[0]+sub(m,j)[0],('jnc',m,C(i,m-1)[1],sub(m,j)[1]))):
            if best is None or cost<best[0]: best=(cost,t)
    for k in range(i+1,j): # cp 項目 k の値で 3 つに分ける (k は 1 つの値の命令)
        if ITEMS[k][1]!=ITEMS[k][2]: continue
        wl,wk,wr=W(i,k-1),w[k],W(k+1,j)
        for cost,t in ((17*wl+27*wk+27*wr+sub(i,k-1)[0]+C(k+1,j)[0],('c_z',k,sub(i,k-1)[1],C(k+1,j)[1])),
                       (17*wk+27*wl+27*wr+sub(i,k-1)[0]+C(k+1,j)[0],('z_c',k,sub(i,k-1)[1],C(k+1,j)[1])),
                       (17*wk+27*wl+27*wr+C(i,k-1)[0]+sub(k+1,j)[0],('z_nc',k,C(i,k-1)[1],sub(k+1,j)[1]))):
            if cost<best[0]: best=(cost,t)
    return best

def show(t,ind=0):
    p='  '*ind; nm=lambda k: ITEMS[k][0]
    if t[0]=='leaf': print(f"{p}→ {nm(t[1])}  ({w[t[1]]:.0f} 回)"); return
    typ,k,a,b=t
    print(p+{'jc':f'cp #{nm(k)}: jp c 左 / 右','jnc':f'cp #{nm(k)}: jp nc 右 / 左',
             'c_z':f'cp #{nm(k)}: jp c 左, jp z {nm(k)} / 右','z_c':f'cp #{nm(k)}: jp z {nm(k)}, jp c 左 / 右',
             'z_nc':f'cp #{nm(k)}: jp z {nm(k)}, jp nc 右 / 左'}[typ]+(f"  ({nm(k)} {w[k]:.0f} 回)" if typ[0] in 'cz' else ''))
    show(a,ind+1); show(b,ind+1)

cost,t=C(0,n-1)
print(f"命令 {sum(w):.0f} 回、1 回平均 {cost/max(1,sum(w)):.1f} クロック")
show(t)
