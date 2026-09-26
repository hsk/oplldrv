# エミュレータの出力 (result) から VGM ファイルを作る
# usage: python3 bin/vgm.py result out.vgm [song.mml]
#   result の opll0 (アドレス) / opll1 (データ) を YM2413 への書き込みに、wait を 1/60 秒 (735 サンプル) に変える
#   出力ファイル名が .vgz なら gzip で圧縮する
#   MML を渡すと #title を GD3 タグの曲名に入れる
import gzip, os, re, struct, sys

OPLL_CLK = 3579545
FRAME = 735  # 44100 / 60

def commands(lines):
    out = bytearray()
    samples = 0
    waits = 0
    addr = 0
    def flush():
        nonlocal waits
        n = waits * FRAME
        while n > 0:
            if n == FRAME: out.append(0x62); n = 0
            else:
                w = min(n, 65535)
                out.extend(struct.pack('<BH', 0x61, w))
                n -= w
        waits = 0
    for l in lines:
        l = l.split()
        if not l: continue
        if l[0] == 'wait':
            waits += 1
            samples += FRAME
        elif l[0] == 'opll0':
            addr = int(l[1])
        elif l[0] == 'opll1':
            flush()
            out += bytes([0x51, addr, int(l[1])])
    flush()
    out.append(0x66)
    return out, samples

def gd3(title):
    # 曲名(英), 曲名(日), ゲーム名(英), ゲーム名(日), システム名(英), システム名(日),
    # 作者(英), 作者(日), 日付, VGM作成者, メモ
    s = [title, '', '', '', 'MSX', '', '', '', '', 'oplldrv', '']
    body = ''.join(x + '\0' for x in s).encode('utf-16-le')
    return b'Gd3 ' + struct.pack('<II', 0x100, len(body)) + body

def title_of(mml):
    with open(mml, encoding='utf-8', errors='replace') as f:
        m = re.search(r'#title\s*{\s*"([^"]*)"\s*}', f.read())
    return m[1].strip() if m else ''

def main():
    if len(sys.argv) < 3:
        print('usage: python3 bin/vgm.py result out.vgm [song.mml]', file=sys.stderr)
        sys.exit(1)
    with open(sys.argv[1]) as f:
        data, samples = commands(f)
    tag = gd3(title_of(sys.argv[3]) if len(sys.argv) > 3 else '')
    HEADER = 0x80  # VGM 1.51 のヘッダサイズ
    head = bytearray(HEADER)
    head[0:4] = b'Vgm '
    struct.pack_into('<I', head, 0x04, HEADER + len(data) + len(tag) - 0x04)  # EOF オフセット
    struct.pack_into('<I', head, 0x08, 0x151)                                 # バージョン
    struct.pack_into('<I', head, 0x10, OPLL_CLK)                              # YM2413 クロック
    struct.pack_into('<I', head, 0x14, HEADER + len(data) - 0x14)             # GD3 オフセット
    struct.pack_into('<I', head, 0x18, samples)                               # 総サンプル数
    struct.pack_into('<I', head, 0x24, 60)                                    # フレームレート
    struct.pack_into('<I', head, 0x34, HEADER - 0x34)                         # データオフセット
    vgm = bytes(head) + bytes(data) + tag
    out = sys.argv[2]
    if out.endswith('.vgz'):
        with gzip.GzipFile(out, 'wb', 9, mtime=0) as f: f.write(vgm)
    else:
        with open(out, 'wb') as f: f.write(vgm)
    print(f'{out}: {samples / 44100:.1f} 秒, {os.path.getsize(out)} バイト')

main()
