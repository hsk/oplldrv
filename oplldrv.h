#include <stdio.h>
#include <stdint.h>
typedef int8_t  s8;
typedef uint8_t u8;
typedef int16_t  s16;
typedef uint16_t u16;
typedef int32_t  s32;
typedef uint32_t u32;
typedef struct PSGDrvCh {
  u8 wait;
  u8* pc;
  u8 tone;
  u8 no10;
  u8 no20;
  u8 no30;
  u8* sp;
  u8 sla;
  u8 sus;  // サスティン (so で 0x20、sf で 0)。0x20+ch に書く値に足す
  u8 drum;
  // ソフトウェア LFO (h a,b,c,d)。PLFO で設定し、PTONEL の音で使う
  u8 lfo;   // 1 で動かす
  u8 key;   // 0x20+ch のキーのビット (PTONEL で 0x10、PKEYOFFL で 0)
  u8 la;    // 遅れ (フレーム)
  u8 lb;    // 振れ幅の段数
  u8 lc;    // 速さ (c+1 フレームで 1 段)
  s8 ld;    // 1 段で F-Number に足す値
  u8 lt;    // 次の段までのフレーム数
  u8 lcnt;  // 折り返すまでの段数
  s8 lstep; // 今の向きの 1 段 (±ld)
  s16 lval; // 今の F-Number のずれ
  u16 lbase;// 音の音程 (block<<9 | F-Number)
  // ポルタメント (c_e4)。PPORTA で始め、N フレームかけて目標の音程まで動かす
  u8 pn;    // 残りのフレーム数。0 なら動かしていない
  u8 pq;    // 1 フレームで動かす F-Number の整数部 (|差| / N)
  u8 pr;    // 余り (|差| % N)。dda で N フレームに r 回、もう 1 動かす
  u8 pnn;   // N
  u8 perr;  // dda の累積
  u8 pdir;  // 1 なら下げる
  s16 pf;   // 今の F-Number (172〜344)
  u8 pblk;  // 今のブロック
} PSGDrvCh;
#define P_WAIT 0
#define P_PC   1
#define P_TONE 3
#define P_NO10 4
#define P_NO20 5
#define P_NO30 6
#define P_SP   7
#define P_SLA  9
#define P_SUS  10
#define P_DRUM 11
#define P_LFO  12
#define P_KEY  13
#define P_LA   14
#define P_LB   15
#define P_LC   16
#define P_LD   17
#define P_LT   18
#define P_LCNT 19
#define P_LSTEP 20
#define P_LVAL 21
#define P_LBASE 23
#define P_PN   25
#define P_PQ   26
#define P_PR   27
#define P_PNN  28
#define P_PERR 29
#define P_PDIR 30
#define P_PF   31
#define P_PBLK 33
#define P_SIZE 34
#define IX(x) x(ix)

#define PDRUM   0x60
#define PKEYOFF 0x80
#define PWAIT   0x81
#define PVOLUME 0x82
#define PEND    0x83
#define PLOOP   0x84
// アセンブラ版は cp で 2 つずつ振り分けるので、よく使う命令ほど前に置く
#define PNEXTS  0x85 // PNEXT の飛び先を符号付き 1 バイトで持つ形
#define PBREAKS 0x86 // PBREAK の飛び先を 1 バイト (0〜255) で持つ形
#define PSLAON  0x87
#define PDRUMV2 0x88 // 0x37 (HH・SD) と 0x38 (TOM・CYM) に同じ音量を書く
#define PDRUMV1 0x89 // 0x37 (HH・SD) に音量を書く
#define PDRUMV  0x8A
#define PNEXT   0x8B
#define PBREAK  0x8C
#define PSLOAD  0x8D
#define PSUSON  0x8E
#define PSUSOFF 0x8F
#define PTONEF  0x90 // デチューンした音。F-Number の下位 8 ビット、ブロックと上位 1 ビット、長さ
#define PTONEL  0x91 // LFO をかける音。PTONEF と同じ形で、LFO をやり直す
#define PKEYOFFL 0x92 // LFO をかけている音のキーオフ。PKEYOFF と同じ形で、キーの状態を覚える
#define PLFO    0x93 // LFO を設定して動かす。遅れ、振れ幅の段数、速さ、1 段の値
#define PLFOOFF 0x94 // LFO を止める
#define PPORTA  0x95 // ポルタメント。始めの F-Number の下位 8 ビット、ブロック<<1 と上位 1 ビット、q、r、N、向き、長さ

void p_play(u8 **bs,u8* stack);
void p_update(void);
unsigned char p_init(void);
