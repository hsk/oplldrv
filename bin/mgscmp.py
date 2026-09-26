"""oplldrv と MGSDRV の OPLL への書き込みを、発音イベントにして比べる

使い方:
    python3 bin/mgscmp.py result ys2_01.mgs.log [-v]

    result      : oplldrv のエミュレータ (6448) の標準出力。opll0 / opll1 / wait の行
    *.mgs.log   : bin/msxplay/mgs2log.mjs の出力。「フレーム opll レジスタ 値」の行
    -v          : 食い違った音符を全部出す (標準は各チャンネル最初の数個)

音符の対応は、MGSDRV の音符の順に、同じ音名・スラーで開始が近い (直前のずれ ±64 フレーム) oplldrv の音符を探して取る。
見つからなければ、開始が 2 フレーム以内の音符と「音名・スラー違い」として対応させる。

書き込みの順番や回数はドライバによって違うので、書き込みをそのまま比べない。
フレームごとにレジスタの状態を再現し、チャンネルごとの音符 (キーオンからキーオフまで) と
リズムの打鍵にしてから比べる。
"""
import math
import sys
from collections import Counter

DRUMS = 'BD SD TOM CYM HH'.split()  # 0x0E のビット 4..0


def read_oplldrv(path):
    """(フレーム, レジスタ, 値) のリスト。wait のたびにフレームを進める"""
    r = []
    frame = 0
    adr = 0
    for line in open(path):
        w = line.split()
        if not w:
            continue
        if w[0] == 'wait':
            frame += 1
        elif w[0] == 'opll0':
            adr = int(w[1])
        elif w[0] == 'opll1':
            r.append((frame, adr, int(w[1])))
    return r, frame


def read_mgs(path):
    r = []
    last = 0
    for line in open(path):
        w = line.split()
        if len(w) == 4 and w[1] == 'opll':
            r.append((int(w[0]), int(w[2]), int(w[3])))
            last = int(w[0])
    return r, last


class Note:
    def __init__(self, ch, start, reg):
        self.ch = ch
        self.start = start
        self.end = None       # キーオフしたフレーム。None = 最後まで鳴りっぱなし
        self.block = (reg[0x20 + ch] >> 1) & 7
        self.fnum = ((reg[0x20 + ch] & 1) << 8) | reg[0x10 + ch]
        self.inst = reg[0x30 + ch] >> 4
        self.vol = reg[0x30 + ch] & 15
        self.sus = (reg[0x20 + ch] >> 5) & 1
        self.legato = False   # キーオフせずに音程だけ変えた (スラー)

    def key(self):
        # 音名で対応を取る (音程表の違いで F-Number が少しずれても同じ音とみなす)
        return (self.name(), self.legato)

    def name(self):
        # F-Number と block から音名を出す (MGSDRV の o4a = 220Hz 基準)
        hz = self.fnum * 3579545 / 72 / (1 << (19 - self.block))
        if hz <= 0:
            return '---'
        n = round(12 * math.log2(hz / 220)) + 57  # o4a = 57
        return 'c c+d d+e f f+g g+a a+b '[n % 12 * 2:n % 12 * 2 + 2].strip() + str(n // 12)


def cents(a, b):
    fa = a.fnum << a.block
    fb = b.fnum << b.block
    if fa == 0 or fb == 0:
        return 0
    return 1200 * math.log2(fa / fb)


def events(writes, end_frame):
    """書き込み → チャンネルごとの音符のリストと、リズムの打鍵のリスト"""
    reg = [0] * 0x40
    notes = {ch: [] for ch in range(9)}
    drums = []  # (フレーム, 楽器名, 音量, リズムの音程レジスタ 0x16〜0x18・0x26〜0x28)
    on = [None] * 9  # 鳴っている音符
    i = 0
    frames = sorted({f for f, a, d in writes})
    for f in frames:
        rise = [False] * 9
        fall = [False] * 9
        pitch = [False] * 9
        drum_rise = 0
        while i < len(writes) and writes[i][0] == f:
            _, a, d = writes[i]
            i += 1
            old = reg[a]
            reg[a] = d
            if 0x20 <= a <= 0x28:
                ch = a - 0x20
                if not old & 0x10 and d & 0x10:
                    rise[ch] = True
                elif old & 0x10 and not d & 0x10:
                    fall[ch] = True
                if (old ^ d) & 0x0f:
                    pitch[ch] = True
            elif 0x10 <= a <= 0x18 and old != d:
                pitch[a - 0x10] = True
            elif a == 0x0e:
                drum_rise |= ~old & d & 0x1f
        # フレームの終わりの状態で判定する
        rhythm = reg[0x0e] & 0x20
        for ch in range(9):
            if rhythm and ch >= 6:
                continue
            keyed = reg[0x20 + ch] & 0x10
            if on[ch] and (fall[ch] or rise[ch] or not keyed):
                on[ch].end = f
                on[ch] = None
            if keyed and (rise[ch] or on[ch] is None):
                on[ch] = Note(ch, f, reg)
                notes[ch].append(on[ch])
            elif keyed and pitch[ch] and on[ch]:
                on[ch].end = f
                on[ch] = Note(ch, f, reg)
                on[ch].legato = True
                notes[ch].append(on[ch])
        for b in range(5):
            if drum_rise & (1 << (4 - b)):
                name = DRUMS[b]
                v = {'BD': reg[0x36] & 15, 'HH': reg[0x37] >> 4, 'SD': reg[0x37] & 15,
                     'TOM': reg[0x38] >> 4, 'CYM': reg[0x38] & 15}[name]
                drums.append((f, name, v, tuple(reg[r] for r in (0x16, 0x17, 0x18, 0x26, 0x27, 0x28))))
    return notes, drums


def match(A, B, key, start, window=64):
    """A (oplldrv) と B (MGSDRV) の対応を取る。B の順に、同じ key で開始が近い A を探す。
    近さは直前に対応したもののずれを基準にするので、ずれが少しずつ増えても追いかけられる。
    difflib だと、繰り返しの多い曲でループ 1 周分ずれた位置と対応させてしまうことがある
    戻り値: (対応した (a, b) のリスト, 対応しなかった A, 対応しなかった B)"""
    pairs = []
    used = set()
    i = 0
    drift = 0
    for b in B:
        expect = start(b) + drift
        best = None
        k = i
        while k < len(A) and start(A[k]) <= expect + window:
            if k not in used and key(A[k]) == key(b) and abs(start(A[k]) - expect) <= window:
                if best is None or abs(start(A[k]) - expect) < abs(start(A[best]) - expect):
                    best = k
            k += 1
        if best is not None:
            pairs.append((A[best], b))
            used.add(best)
            drift = start(A[best]) - start(b)
            while i in used:
                i += 1
    ua = [a for k, a in enumerate(A) if k not in used]
    ub_set = {id(b) for a, b in pairs}
    ub = [b for b in B if id(b) not in ub_set]
    return pairs, ua, ub


def compare(name, a_path, b_path, verbose=False):
    aw, a_end = read_oplldrv(a_path)
    bw, b_end = read_mgs(b_path)
    end = min(a_end, b_end)
    aw = [w for w in aw if w[0] <= end]
    bw = [w for w in bw if w[0] <= end]
    an, ad = events(aw, end)
    bn, bd = events(bw, end)
    print(f'== {name}  ({end} フレームまで比べる。左 oplldrv / 右 MGSDRV)')
    total = Counter()
    fnums = Counter()
    for ch in range(9):
        A, B = an[ch], bn[ch]
        if not A and not B:
            continue
        pairs, ua, ub = match(A, B, lambda n: n.key(), lambda n: n.start)
        # 対応しなかったもののうち、開始がほぼ同じ (2 フレーム以内) ものは、音名かスラーが違うものとして対応させる
        pitch_ng, ua, ub = match(ua, ub, lambda n: 0, lambda n: n.start, window=2)
        pairs_all = sorted(pairs + pitch_ng, key=lambda p: p[1].start)
        fnum_ng = Counter((a.name(), a.fnum, b.fnum) for a, b in pairs if (a.block, a.fnum) != (b.block, b.fnum))
        dt = [a.start - b.start for a, b in pairs_all]
        dlen = [((a.end or end) - a.start) - ((b.end or end) - b.start) for a, b in pairs_all]
        inst_ng = [(a, b) for a, b in pairs_all if (a.inst, a.vol) != (b.inst, b.vol)]
        # 鳴りっぱなし: MGSDRV では止まっているのに oplldrv では最後まで鳴っている / 長すぎる
        # oplldrv の音が MGSDRV と同じ長さなら比較範囲の中で止まるはずなのに、止まっていないもの。
        # 範囲の終わり近くのものは、タイミングのずれで範囲の外で止まっていることがあるので数えない
        hang = [(a, b) for a, b in pairs_all
                if a.end is None and b.end is not None and a.start + (b.end - b.start) < end - 30]
        long_ = [(a, b) for a, b in pairs_all if a.end is not None and b.end is not None
                 and (a.end - a.start) - (b.end - b.start) >= 8]
        unmatched_a = len(A) - len(pairs_all)
        unmatched_b = len(B) - len(pairs_all)
        fnums.update(fnum_ng)
        c = [cents(a, b) for a, b in pairs_all if a.fnum and b.fnum]
        cmean = sum(c) / len(c) if c else 0
        total.update(notes_a=len(A), notes_b=len(B), match=len(pairs), pitch=len(pitch_ng),
                     fnum=sum(fnum_ng.values()),
                     hang=len(hang), long=len(long_), inst=len(inst_ng))
        print(f'  ch{ch}: 音符 {len(A)}/{len(B)} 一致 {len(pairs)} 音名・スラー違い {len(pitch_ng)} '
              f'対応なし {unmatched_a}/{unmatched_b} | '
              f'開始のずれ {min(dt, default=0)}..{max(dt, default=0)} 最後 {dt[-1] if dt else 0} | '
              f'長さの差 {min(dlen, default=0)}..{max(dlen, default=0)} | '
              f'F-Number違い {sum(fnum_ng.values())} 平均 {cmean:+.1f} セント | 音色音量違い {len(inst_ng)} | '
              f'鳴りっぱなし {len(hang)} 長すぎ(8フレーム以上) {len(long_)}')
        lim = None if verbose else 3
        for label, lst in (('音名・スラー違い', pitch_ng), ('音色音量違い', inst_ng),
                           ('鳴りっぱなし', hang), ('長すぎ', long_)):
            for a, b in lst[:lim]:
                print(f'      {label}: oplldrv {a.start}-{a.end} {a.name()} blk{a.block} fn{a.fnum} '
                      f'@{a.inst} v{a.vol} sus{a.sus}{" &" if a.legato else ""} / '
                      f'MGSDRV {b.start}-{b.end} {b.name()} blk{b.block} fn{b.fnum} '
                      f'@{b.inst} v{b.vol} sus{b.sus}{" &" if b.legato else ""}')
        for a in ua[:lim]:
            print(f'      対応なし: oplldrv {a.start}-{a.end} {a.name()}{" &" if a.legato else ""}')
        for b in ub[:lim]:
            print(f'      対応なし: MGSDRV {b.start}-{b.end} {b.name()}{" &" if b.legato else ""}')
    if ad or bd:
        pairs, _, _ = match(ad, bd, lambda d: d[1], lambda d: d[0])
        dt = [a[0] - b[0] for a, b in pairs]
        vol_ng = [(a, b) for a, b in pairs if a[2] != b[2]]
        pitch_ng = [(a, b) for a, b in pairs if a[3] != b[3]]
        print(f'  リズム: 打鍵 {len(ad)}/{len(bd)} 一致 {len(pairs)} '
              f'開始のずれ {min(dt, default=0)}..{max(dt, default=0)} 音量違い {len(vol_ng)} '
              f'音程レジスタ違い {len(pitch_ng)}')
        lim = None if verbose else 3
        for label, lst in (('音量違い', vol_ng), ('音程レジスタ違い', pitch_ng)):
            for a, b in lst[:lim]:
                print(f'      {label}: oplldrv {a[0]} {a[1]} v{a[2]} '
                      f'[{" ".join(f"{x:02x}" for x in a[3])}] / MGSDRV {b[0]} {b[1]} v{b[2]} '
                      f'[{" ".join(f"{x:02x}" for x in b[3])}]')
        total.update(drum_a=len(ad), drum_b=len(bd), drum_match=len(pairs), drum_vol=len(vol_ng),
                     drum_pitch=len(pitch_ng))
    if fnums:
        print('  F-Number の違い (音名 oplldrv MGSDRV 回数): ' + ' '.join(
            f'{n}:{a}/{b}x{c}' for (n, a, b), c in sorted(fnums.items(), key=lambda x: -x[1])[:12]))
    n = max(total['notes_b'], 1)
    print(f'  合計: 音符一致 {total["match"]}/{total["notes_b"]} ({total["match"] * 100 / n:.1f}%) '
          f'音名・スラー違い {total["pitch"]} F-Number違い {total["fnum"]} 音色音量違い {total["inst"]} '
          f'鳴りっぱなし {total["hang"]} 長すぎ {total["long"]}'
          + (f' リズム一致 {total["drum_match"]}/{total["drum_b"]} 音量違い {total["drum_vol"]}'
             f' 音程レジスタ違い {total["drum_pitch"]}'
             if total['drum_b'] or total['drum_a'] else ''))
    return total


if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if not a.startswith('-')]
    compare(args[1].split('/')[-1].split('.')[0], args[0], args[1], '-v' in sys.argv)
