#!/usr/bin/env node
// MML を MGSDRV (msxplay.com と同じ mgsc-js + libkss-js) で鳴らし、OPLL と PSG への書き込みのログを出す
// oplldrv のログ (bin/mgscmp.py) と比べるためのもの
//
// 使い方:
//   node bin/msxplay/mgs2log.mjs res/ys2_01.mml --frames 6673 > ys2_01.mgs.log
//     --frames N  N フレーム (1/60 秒) 鳴らす (標準 3600)
//     --wav FILE  WAV も書き出す
//
// 出力: 1 行 1 書き込み
//   <フレーム番号> opll <レジスタ> <値>
//   <フレーム番号> psg <レジスタ> <値>
// フレーム番号 = VGM のサンプル数 / 735 (44100Hz の 1/60 秒)
import fs from 'fs';
import mgsc from 'mgsc-js';
import { KSS, KSSPlay } from 'libkss-js';

const { MGSC, decodeText } = mgsc;

const opt = { frames: 3600 };
const argv = process.argv.slice(2);
for (let i = 0; i < argv.length; i++) {
  if (argv[i] === '--frames') opt.frames = Number(argv[++i]);
  else if (argv[i] === '--wav') opt.wav = argv[++i];
  else opt.input = argv[i];
}
if (!opt.input) {
  console.error('usage: mgs2log.mjs input.mml [--frames N] [--wav out.wav]');
  process.exit(2);
}

await MGSC.initialize();
const result = MGSC.compile(decodeText(fs.readFileSync(opt.input)));
if (!result.success) {
  const e = result.errorInfo;
  console.error(`MGSC エラー: ${opt.input}:${e ? e.lineNumber : '?'}: ${e ? e.message : result.rawMessage}`);
  process.exit(1);
}

await KSSPlay.initialize();
const kss = KSS.createUniqueInstance(result.mgs, opt.input.replace(/\.mml$/i, '.mgs'));
const vgm = await kss.toVGMAsync({ duration: Math.ceil(opt.frames * 1000 / 60), loop: 256 });

// --- VGM を読んで、書き込みをフレーム番号付きで出す
const v = new DataView(vgm.buffer, vgm.byteOffset, vgm.byteLength);
let pos = v.getUint32(0x34, true) ? 0x34 + v.getUint32(0x34, true) : 0x40;
let samples = 0;
const out = [];
const log = (chip, a, d) => out.push(`${Math.floor(samples / 735)} ${chip} ${a} ${d}`);
loop: while (pos < vgm.length) {
  const c = vgm[pos];
  switch (true) {
    case c === 0x51: log('opll', vgm[pos + 1], vgm[pos + 2]); pos += 3; break;
    case c === 0xA0: log('psg', vgm[pos + 1], vgm[pos + 2]); pos += 3; break;
    case c === 0x61: samples += v.getUint16(pos + 1, true); pos += 3; break;
    case c === 0x62: samples += 735; pos += 1; break;
    case c === 0x63: samples += 882; pos += 1; break;
    case (c & 0xF0) === 0x70: samples += (c & 15) + 1; pos += 1; break;
    case c === 0x66: break loop;
    case c === 0x67: pos += 7 + v.getUint32(pos + 3, true); break;
    case c === 0x4F || c === 0x50: pos += 2; break;
    case c >= 0x52 && c <= 0x5F: pos += 3; break;
    case c >= 0xA1 && c <= 0xBF: pos += 3; break;
    case c >= 0xC0 && c <= 0xDF: pos += 4; break;
    case c >= 0xE0: pos += 5; break;
    default: console.error(`VGM: 知らないコマンド 0x${c.toString(16)} (${pos})`); process.exit(1);
  }
}
process.stdout.write(out.join('\n') + '\n');

if (opt.wav) {
  const RATE = 44100;
  const player = new KSSPlay(RATE);
  player.setData(kss);
  player.setDeviceQuality({ psg: 1, scc: 1, opll: 1, opl: 1 });
  player.reset(null);
  const pcm = player.calc(Math.ceil(opt.frames * RATE / 60));
  player.release();
  const h = Buffer.alloc(44);
  h.write('RIFF', 0); h.writeUInt32LE(36 + pcm.length * 2, 4); h.write('WAVE', 8);
  h.write('fmt ', 12); h.writeUInt32LE(16, 16); h.writeUInt16LE(1, 20); h.writeUInt16LE(1, 22);
  h.writeUInt32LE(RATE, 24); h.writeUInt32LE(RATE * 2, 28); h.writeUInt16LE(2, 32); h.writeUInt16LE(16, 34);
  h.write('data', 36); h.writeUInt32LE(pcm.length * 2, 40);
  fs.writeFileSync(opt.wav, Buffer.concat([h, Buffer.from(pcm.buffer, pcm.byteOffset, pcm.byteLength)]));
}
kss.release();
