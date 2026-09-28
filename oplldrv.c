#include "oplldrv.h"
//#define OPT 1
//#define OPT2 1 
//#ifdef DEVKITSMS

u8* sound;
PSGDrvCh psgdrv[9];
u8 lfo_used; // PLFO か PPORTA を 1 度でも実行したら 1。0 の間はフレームごとの LFO・ポルタメントの処理をしない
__sfr __at 0xF0 IOPortOPLL1;
__sfr __at 0xF1 IOPortOPLL2;

#define ym2413(reg,parm) {IOPortOPLL1 = (reg);IOPortOPLL2 = (parm);}
// 音程表: block<<9 | F-Number。F-Number は MGSDRV と同じ値 (o4a = 290)
static u16 const tones[] = {
   172,  182,  194,  205,  217,  230,  244,  258,  273,  290,  307,  325,
   684,  694,  706,  717,  729,  742,  756,  770,  785,  802,  819,  837,
  1196, 1206, 1218, 1229, 1241, 1254, 1268, 1282, 1297, 1314, 1331, 1349,
  1708, 1718, 1730, 1741, 1753, 1766, 1780, 1794, 1809, 1826, 1843, 1861,
  2220, 2230, 2242, 2253, 2265, 2278, 2292, 2306, 2321, 2338, 2355, 2373,
  2732, 2742, 2754, 2765, 2777, 2790, 2804, 2818, 2833, 2850, 2867, 2885,
  3244, 3254, 3266, 3277, 3289, 3302, 3316, 3330, 3345, 3362, 3379, 3397,
  3756, 3766, 3778, 3789, 3801, 3814, 3828, 3842, 3857, 3874, 3891, 3909,
};
#ifdef PSG
// PSG (MSX の AY-3-8910)。-D PSG=1 のときだけ入る
__sfr __at 0xA0 IOPortPSG1;
__sfr __at 0xA1 IOPortPSG2;
#define ay(reg,parm) {IOPortPSG1 = (reg);IOPortPSG2 = (parm);}
// 音程表: 周期。MGSDRV と同じ値 (o4a = 254、440Hz)
static u16 const psg_tones[] = {
  3421, 3228, 3047, 2876, 2715, 2562, 2419, 2283, 2155, 2034, 1920, 1812,
  1710, 1614, 1523, 1438, 1357, 1281, 1209, 1141, 1077, 1017,  960,  906,
   855,  807,  761,  719,  678,  640,  604,  570,  538,  508,  480,  453,
   427,  403,  380,  359,  339,  320,  302,  285,  269,  254,  240,  226,
   213,  201,  190,  179,  169,  160,  151,  142,  134,  127,  120,  113,
   106,  100,   95,   89,   84,   80,   75,   71,   67,   63,   60,   56,
    53,   50,   47,   44,   42,   40,   37,   35,   33,   31,   30,   28,
    26,   25,   23,   22,   21,   20,   18,   17,   16,   15,   15,   14,
};
// PSG のチャンネル
typedef struct PSGCh {
  u8 wait;
  u8* pc;
  u8* sp;
  u8 vol;   // v (0〜15)
  u8 reg;   // 周期のレジスタ (2ch)
  u8 vreg;  // 音量のレジスタ (8+ch)
  u8 sla;   // & でつなぐ音の前なら 1
  // @r の ADSR (MGSDRV と同じ)。レベル e (0〜255) を毎フレーム動かし、音量は floor(e * (v+1) / 256)
  u8 env;   // 段階。0 なら ADSR を使わない (音量一定で、キーオフで音量 0)
  u8 koff;  // キーオフしたら 1
  u8 e;     // レベル
  u8 ei, ea, ed, es, esr, er; // 初期値、AR、DR、SL、SR、RR
  u8 out;   // 最後に書いた音量
  // ソフトウェア LFO (h a,b,c,d)。FM と同じ三角波で、周期からずれを引く (周期なので向きが逆)
  u8 lfo;   // 1 で動かす
  u8 la, lb, lc; s8 ld; // 遅れ、振れ幅の段数、速さ、1 段の値
  u8 lt;    // 次の段までのフレーム数
  u8 lcnt;  // 折り返すまでの段数
  s8 lstep; // 今の向きの 1 段 (±ld)
  s16 lval; // 今のずれ
  u16 base; // 音の周期
  u8 note;  // 音程表の番号 (同じ音程を & でつなぐときは LFO を続ける)
} PSGCh;
#define ENV_A 1 // アタック: +AR。255 になったらディケイへ
#define ENV_D 2 // ディケイ: -DR。SL になったらサステインへ
#define ENV_S 3 // サステイン: -SR。0 になったらリリースへ。キーオフしていればリリースへ
#define ENV_R 4 // リリース: -RR
PSGCh psgch[3];
u8 psg_size;
// ADSR を 1 段進めて、音量が変わっていれば書く。キーオフしてもアタックとディケイは続ける (MGSDRV と同じ)
static void psg_env(PSGCh* ch) {
  u8 e=ch->e;
  if (ch->env==ENV_S && ch->koff) ch->env=ENV_R;
  switch (ch->env) {
  case ENV_A: e = (e+ch->ea>255) ? 255 : e+ch->ea;
              if (e==255) ch->env=ENV_D;
              break;
  case ENV_D: e = (e<ch->ed || e-ch->ed<ch->es) ? ch->es : e-ch->ed;
              if (e==ch->es) ch->env=ENV_S;
              break;
  case ENV_S: e = (e<ch->esr) ? 0 : e-ch->esr;
              if (!e) ch->env=ENV_R;
              break;
  default:    e = (e<ch->er) ? 0 : e-ch->er;
  }
  ch->e=e;
  u8 v=(u8)(((u16)e*(ch->vol+1))>>8);
  if (v!=ch->out) { ch->out=v; ay(ch->vreg,v); }
}
// PSG のチャンネルを 1 フレーム進める。音符は周期と音量を書き、キーオフは音量 0
static void p_exec_psg(PSGCh* ch) {
  u16 bc;
  if (--ch->wait) {
    return;
  }
  ch->wait++;
  while (1) {
    u8 a = *ch->pc++;
    if (a < PDRUM) {
      // 同じ音程を & でつなぐときは、音程も LFO もそのまま (FM と同じ)
      if (!ch->sla || a!=ch->note) {
        // LFO をやり直す。& でつなぐときは速さのタイマーを続ける (FM と同じ)
        if (!ch->sla) ch->lt=ch->la+ch->lc+2;
        ch->lval=0; ch->lstep=ch->ld; ch->lcnt=(u8)((ch->lb+1)>>1);
        u16 t = psg_tones[a];
        ch->note=a; ch->base=t;
        ay(ch->reg,(u8)t);
        ay(ch->reg+1,(u8)(t>>8));
      }
      if (!ch->env) ay(ch->vreg,ch->vol)
      else if (ch->sla) psg_env(ch); // & でつなぐ音はやり直さず、このフレームはもう 1 段進める (MGSDRV と同じ)
      else { ch->e=ch->ei; ch->env=ENV_A; ch->koff=0; }
      ch->sla=0;
      a=*ch->pc++;ch->wait=a;
      return;
    }
    switch (a) {
    case PKEYOFF: if (ch->env) ch->koff=1; else ay(ch->vreg,0);
    case PWAIT: a=*ch->pc++;ch->wait=a; return;
    case PVOLUME: ch->vol=*ch->pc++; break; // 音量は次の音 (ADSR なら次の段) で書く
    case PEND:  ch->pc--; ch->wait=0; ch->env=0; ch->lfo=0; ay(ch->vreg,0); return;
    case PLOOP: *(++ch->sp) = *ch->pc++; *(++ch->sp) = *ch->pc++; break;
    case PNEXTS: bc = (u16)(s16)(s8)*ch->pc++; goto pnext;
    case PNEXT: bc = *(u16*)ch->pc; ch->pc+=2;
    pnext:      (*ch->sp)--;
                if(*ch->sp) {
                  if(*ch->sp==255) (*ch->sp)++; // 無限ループ。FM と違ってキーオフはしない
                  // dda wait
                  u8 a = *ch->pc++;
                  a += ch->sp[-1];
                  u8 e = *ch->pc;
                  ch->pc += bc;
                  if (e <= a) {
                    a -= e;
                    ch->sp[-1]=a;
                    return;
                  }
                  ch->sp[-1]=a;
                  break;
                }
                ch->sp-=2;
                {// dda wait
                  u8 a = *ch->pc++;
                  a += ch->sp[1];
                  u8 c = *ch->pc++;
                  if (c <= a) {
                    a -= c;
                    ch->sp[1]=a;
                    return;
                  }
                  ch->sp[1]=a;
                }
                break;
    case PBREAKS:if (*ch->sp == 1) { bc = *ch->pc; goto pbreak; }
                ch->pc+=1;
                break;
    case PBREAK:if (*ch->sp == 1) {
                  bc = *(u16*)ch->pc;
    pbreak:       ch->pc += bc;
                  // add dda
                  ch->sp-=2;
                  u8 a = *ch->pc++;
                  a += ch->sp[1];
                  u8 c = *ch->pc++;
                  if (c <= a) {
                    a -= c;
                    ch->sp[1]=a;
                    return;
                  }
                  ch->sp[1]=a;
                  break;
                }
                ch->pc+=2;
                break;
    case PSLAON: ch->sla=1; break;
    case PLFO:  ch->la=*ch->pc++; ch->lb=*ch->pc++; ch->lc=*ch->pc++; ch->ld=*ch->pc++;
                ch->lfo=1;
                ch->lt=ch->la+ch->lc+2;
                ch->lval=0; ch->lstep=ch->ld; ch->lcnt=(u8)((ch->lb+1)>>1);
                break;
    case PLFOOFF: ch->lfo=0; break;
    case PSLOAD: // @n: @rn の ADSR (初期値、AR、DR、SL、SR、RR) を使う。レベルは 0 にする (MGSDRV と同じ)
                ch->ei=*ch->pc++; ch->ea=*ch->pc++; ch->ed=*ch->pc++;
                ch->es=*ch->pc++; ch->esr=*ch->pc++; ch->er=*ch->pc++;
                ch->e=0; ch->env=ENV_R; ch->out=255;
                break;
    }
  }
}
static void psg_play(u8 **bs,u8* sp,u8 n) {
  PSGCh *p = psgch;
  psg_size=n;
  ay(7,0xB8); // トーンだけ出す (ノイズなし)
  for(u8 i=0;i<3;i++) ay(8+i,0);
  for(u8 i=0;i<n;i++,p++) {
    p->pc=bs[i]+1;
    p->wait=1;
    p->vol=0;
    p->reg=i+i;
    p->vreg=8+i;
    p->sla=0;
    p->env=0;
    p->lfo=0;
    p->la=p->lb=p->lc=p->ld=0;
    p->note=255;
    p->sp=sp-1;
    sp += bs[i][0]*2;
  }
}
// LFO を 1 フレーム進める (MGSDRV と同じく、命令を読む前)
static void psg_lfo(PSGCh* ch) {
  if (--ch->lt) return;
  ch->lt=ch->lc+1;
  if (!ch->lcnt) { ch->lstep=-ch->lstep; ch->lcnt=ch->lb+1; } // 三角波の折り返し
  ch->lcnt--;
  ch->lval+=ch->lstep;
  u16 t=ch->base-ch->lval;
  ay(ch->reg,(u8)t);
  ay(ch->reg+1,(u8)(t>>8));
}
// PSG を 1 フレーム進める。LFO を進め、命令を読んでから ADSR を 1 段進める
// (MGSDRV は ADSR を先に進めてから命令を読み、音のときはもう 1 段進める。休符のキーオフが 1 フレーム遅れるのはコンパイラが合わせる)
static void psg_update(void) {
  PSGCh *p = psgch;
  for(u8 i=psg_size;i;i--,p++) {
    if (p->lfo) psg_lfo(p);
    p_exec_psg(p);
    if (p->env) psg_env(p);
  }
}
// ヘッダの上位バイト: ビット 0 はリズムモード、ビット 1〜2 は PSG のチャンネル数
#define HDR_MODE(h) ((h)&1)
#else
#define HDR_MODE(h) (h)
#endif
#ifdef DEVKITSMS
unsigned char p_init (void) __naked {
  __asm

    // first we need to perform region detection
    // as devkitSMS currently does NOT support that :|

    ld a, #0b11110101               // Output 1s on both TH lines
    out (#0x3f), a
    in a, (#0xdd)
    and #0b11000000                 // See what the TH inputs are
    cp #0b11000000                  // If the input does not match the output then it is a Japanese system
    jp nz, _IsJapanese

    ld a, #0b01010101               // Output 0s on both TH lines
    out (#0x3f), a
    in a, (#0xdd)
    and #0b11000000                 // See what the TH inputs are
    jp nz, _IsJapanese              // If the input does not match the output then it is a Japanese system

    ld a, #0b11111111               // Set everything back to being inputs
    out (#0x3f), a

    ld e, #1                        // export = 1
    jr _getAudioCap

_IsJapanese:
    ld e, #0                        // export = 0

_getAudioCap:
    ld a, (_SMS_Port3EBIOSvalue)
    or #0x04                        // disable I/O chip
    out (#0x3E), a

    ld bc, #0                       // reset counters

_next:
    ld a, b
    out (#0xF2), a                  // output to the audio control port

    in a, (#0xF2)                   // read back
    and #0b00000011                 // mask to bits 0-1 only
    cp b                            // check what is read is the same as what was written
    jr nz, _noinc

    inc c                           // c = # of times the result is the same

_noinc:
    inc b                           // increase counter
    bit 2, b                        // repeated four times?
    jr z, _next                     // no? then repeat again

    ld a, (_SMS_Port3EBIOSvalue)
    out (#0x3E), a                  // turn I/O chip back on

    srl c                           // 4 --> 2; 3, 2 --> 1; 0, 1 --> 0
    ld a, c
    bit 0, c                        // check if PSG+FM (Japanese SMS) or PSG only
    jr z, _done                     // yes? then transfer value directly

    add a,e                         // else check region: if Region = 1 (Export)
                                    // then 1 --> 2 (3rd party FM board);
                                    // else 1 stays 1 unchanged (Mark III + FM unit)
_done:
    ld l, a                         // return FM type
    ret
  __endasm;
}
#endif
#ifndef OPT
void p_exec(PSGDrvCh* ch) {
  u16 bc;
  if (--ch->wait) {
    return;
  }
  ch->wait++;
  while (1) {
    u8 a = *ch->pc++;
    if (a < PDRUM) {
      if (!ch->sla) {
        ym2413(ch->no20,0);
      }
      ch->sla=0;
      a=a+a;
      u8* iy = &((u8*)tones)[a];
      a = iy[0];
      ym2413(ch->no10,a);
      a = iy[1]|ch->sus;
      ch->tone=a; 
      ym2413(ch->no20,(1<<4)|a);
      a=*ch->pc++;ch->wait=a;
      return;
    }
    if (a < PKEYOFF) {
      ym2413(0x0e,(1<<5)|0);
      ym2413(0x0e,(a&0x3f));
      a=*ch->pc++;
      ch->wait=a;
      return;
    }
    switch (a) {
    case PKEYOFF: ym2413(ch->no20,ch->tone);
    case PWAIT: a=*ch->pc++;ch->wait=a; return;
    case PVOLUME: a=*ch->pc++; ym2413(ch->no30, a); break;
    case PEND:  ch->pc--; ch->wait=0;ch->lfo=0;ch->pn=0;ym2413(ch->no20,0); return;
    case PLOOP: *(++ch->sp) = *ch->pc++; *(++ch->sp) = *ch->pc++; break;
    case PNEXTS: bc = (u16)(s16)(s8)*ch->pc++; goto pnext; // 飛び先が近いときの 1 バイトの形
    case PNEXT: bc = *(u16*)ch->pc; ch->pc+=2;
    pnext:      (*ch->sp)--;
                if(*ch->sp) {
                  if(*ch->sp==255) {
                    (*ch->sp)++;
                    if(ch->drum) {ym2413(0x0e,(1<<5)|0);}
                    else {ym2413(ch->no20,0);}
                  }
                  // dda wait
                  u8 a = *ch->pc++;
                  a += ch->sp[-1];
                  u8 e = *ch->pc;
                  ch->pc += bc;
                  if (e <= a) {
                    a -= e;
                    ch->sp[-1]=a;
                    return;
                  }
                  ch->sp[-1]=a;
                  break;
                }
                ch->sp-=2;
                {// dda wait
                  u8 a = *ch->pc++;
                  a += ch->sp[1];
                  u8 c = *ch->pc++;
                  if (c <= a) {
                    a -= c;
                    ch->sp[1]=a;
                    return;
                  }
                  ch->sp[1]=a;
                }
                break; 
    case PBREAKS:if (*ch->sp == 1) { bc = *ch->pc; goto pbreak; } // 飛び先が近いときの 1 バイトの形
                ch->pc+=1;
                break;
    case PBREAK:if (*ch->sp == 1) {
                  bc = *(u16*)ch->pc;
    pbreak:       ch->pc += bc;
                  // add dda
                  ch->sp-=2;
                  u8 a = *ch->pc++;
                  a += ch->sp[1];
                  u8 c = *ch->pc++;
                  if (c <= a) {
                    a -= c;
                    ch->sp[1]=a;
                    return;
                  }
                  ch->sp[1]=a;
                  break;
                }
                ch->pc+=2;
                break;
    case PSLOAD:{
                  u8* de = &sound[*ch->pc++];
                  ym2413(0,*de++);
                  ym2413(1,*de++);
                  ym2413(2,*de++);
                  ym2413(3,*de++);
                  ym2413(4,*de++);
                  ym2413(5,*de++);
                  ym2413(6,*de++);
                  ym2413(7,*de);
                  break;
                }
    case PSLAON: ch->sla=1; break;
    case PSUSON: ch->sus=0x20; break;
    case PSUSOFF: ch->sus=0; break;
    case PTONEL:{ // LFO をかける音。スラーでつなぐときは速さのタイマーを続ける
                  if (!ch->sla) {
                    ym2413(ch->no20,0);
                    ch->lt=ch->la+ch->lc+2;
                  }
                  ch->sla=0;
                  ch->key=0x10;
                  ch->lbase=*(u16*)ch->pc;
                  ch->lval=0;
                  ch->lstep=ch->ld;
                  ch->lcnt=(u8)((ch->lb+1)>>1);
                  ym2413(ch->no10,*ch->pc++);
                  a = *ch->pc++|ch->sus;
                  ch->tone=a;
                  ym2413(ch->no20,(1<<4)|a);
                  a=*ch->pc++;ch->wait=a;
                  return;
                }
    case PKEYOFFL: ch->key=0; ym2413(ch->no20,ch->tone); a=*ch->pc++;ch->wait=a; return;
    case PLFO:  ch->la=*ch->pc++; ch->lb=*ch->pc++; ch->lc=*ch->pc++; ch->ld=*ch->pc++;
                ch->lfo=1; lfo_used=1;
                ch->lt=ch->la+ch->lc+2;
                ch->lval=0; ch->lstep=ch->ld; ch->lcnt=(u8)((ch->lb+1)>>1);
                break;
    case PLFOOFF: ch->lfo=0; break;
    case PPORTA:{ // ポルタメント。始めの音程で鳴らし、フレームごとの処理で目標まで動かす
                  if (!ch->sla) {
                    ym2413(ch->no20,0);
                  }
                  ch->sla=0;
                  ch->key=0x10;
                  u8 lo=*ch->pc++; u8 hi=*ch->pc++;
                  ch->pf=lo|((u16)(hi&1)<<8); ch->pblk=hi>>1;
                  ch->pq=*ch->pc++; ch->pr=*ch->pc++; ch->pnn=ch->pn=*ch->pc++; ch->pdir=*ch->pc++;
                  ch->perr=0; lfo_used=1;
                  ym2413(ch->no10,lo);
                  a = hi|ch->sus;
                  ch->tone=a;
                  ym2413(ch->no20,(1<<4)|a);
                  a=*ch->pc++;ch->wait=a;
                  return;
                }
    case PTONEF:{ // デチューンした音。音程をデータで持つ
                  if (!ch->sla) {
                    ym2413(ch->no20,0);
                  }
                  ch->sla=0;
                  ym2413(ch->no10,*ch->pc++);
                  a = *ch->pc++|ch->sus;
                  ch->tone=a;
                  ym2413(ch->no20,(1<<4)|a);
                  a=*ch->pc++;ch->wait=a;
                  return;
                }
    case PDRUMV2:{ // 0x37 と 0x38 に同じ音量を書く
                  u8 v=*ch->pc++;
                  ym2413(0x37,v);
                  ym2413(0x38,v);
                  break;
                }
    case PDRUMV1:{ // 0x37 に音量を書く
                  u8 v=*ch->pc++;
                  ym2413(0x37,v);
                  break;
                }
    case PDRUMV:{
                  u8 n=*ch->pc++;
                  u8 v=*ch->pc++;
                  ym2413(n,v);
                  break;
                }
    }
  }
}
#else
#define $ __endasm;__asm
void p_exec(PSGDrvCh* ch) __naked {
  ch;
  __asm
  dec (hl) $ ret nz $ inc (hl)
  push ix $ push hl $ pop ix $ ld l,P_PC(ix) $ ld h,P_PC+1(ix)
  1$:; while (1) {
    ld a,(hl) $ inc hl; u8 a = *ch->pc++;
    ; switch (a
      cp #PDRUM $ jp c, 3$
      cp #PKEYOFF $ jp c,12$ $ jp z,4$
      cp #PVOLUME $ jp c,5$ $ jp z,6$
      cp #PLOOP $ jp c,7$ $ jp z,8$
      cp #PBREAKS $ jp c,16$ $ jp z,17$
      cp #PDRUMV2 $ jp c,13$ $ jp z,18$
      cp #PDRUMV $ jp c,19$ $ jp z,15$
      cp #PBREAK $ jp c,9$ $ jp z,10$
      cp #PSUSON $ jp c,11$ $ jp z,14$
      cp #PTONEF $ jp c,20$ $ jp z,21$
      cp #PKEYOFFL $ jp c,22$ $ jp z,23$
      cp #PLFOOFF $ jp c,24$ $ jp z,25$ $ jp 26$
    ; ) {
    3$:; case PTONE:
      ld d,a
      ld a,IX(P_NO20) $ ld c,a
      xor a $ cp IX(P_SLA) $ jp nz, 31$; if (!ch->sla) {
        ; ym2413(ch->no20,0);  
        ld a,c $ out (_IOPortOPLL1), a
        xor a $ out (_IOPortOPLL2), a
      31$: ; }
      ld IX(P_SLA),#0 ; ch->sla=0
      ld a,d $ add a,a ; a=a+a;
      ; u8* iy = &((u8*)tones)[a];
      add a, #<(_tones) $ ld e, a $ ld a, #0x00 $ adc a, #>(_tones) $ ld d, a
      //ld de,#_tones $ add e $ ld e,a $ jr nc, 55$ $ inc d $ 55$:
    32$: ; PTONEF はここから同じ
      ; a = iy[0];
      ; ym2413(0x10+ch->no,a);
        ld a,IX(P_NO10) $ out (_IOPortOPLL1), a
      ld a,(de) $ inc de $ out (_IOPortOPLL2), a
      ; a = iy[1];
      ; ch->tone=a;
      ; ym2413(0x20+ch->no,(1<<4)|(a));
      ld a,c $ out (_IOPortOPLL1), a
      ld a,(de) $ or IX(P_SUS) $ ld IX(P_TONE), a $ or a, #16 $ out (_IOPortOPLL2), a
      ld a,(hl) $ inc hl $ ld IX(P_WAIT),a 
      jp 2$; break;
    4$:; case PKEYOFF:
      ; ym2413(0x20+ch->no,ch->tone);
      ld a,IX(P_NO20) $ out (_IOPortOPLL1), a
      ld a,IX(P_TONE) $ out	(_IOPortOPLL2), a
    5$:; case PWAIT:
      ld a,(hl) $ inc hl $ ld IX(P_WAIT),a; ch->wait=*ch->pc++
      jp 2$; return;
    6$:; case PVOLUME:
      ; ym2413(0x30+ch->no,*ch->pc++);
      ld a,IX(P_NO30) $ out (_IOPortOPLL1), a
      ld a,(hl) $ inc hl $ out (_IOPortOPLL2), a
      jp 1$; break;
    7$: dec hl $ ld IX(P_WAIT),#0 ; case PEND:  ch->pc--;
      ld IX(P_LFO),#0 ; ch->lfo=0
      ld IX(P_PN),#0 ; ch->pn=0
      ; ym2413(ch->no20,0)
      ld a,IX(P_NO20) $ out (_IOPortOPLL1), a
      xor a $ out	(_IOPortOPLL2), a
      jp 2$  ; return; (10)
    8$: ; case PLOOP:
      ld e,IX(P_SP) $ ld d,IX(P_SP+1)
      ; *(++ch->sp) = *ch->pc++
      inc de $ ld a,(hl) $ inc hl $ ld (de), a
      ; *(++ch->sp) = *ch->pc++
      inc de $ ld a,(hl) $ inc hl $ ld (de), a
      ld IX(P_SP),e $ ld IX(P_SP+1),d
      jp 1$; break;
    16$: ;case PNEXTS:
      ld a,(hl) $ inc hl $ ld c,a $ rla $ sbc a,a $ ld b,a ; bc = (s8)*ch->pc++;
      jp 90$
    9$: ;case PNEXT:
      ld c,(hl) $ inc hl $ ld b,(hl) $ inc hl; u16 bc = *(u16*)ch->pc; ch->pc+=2
    90$:
      ld e,IX(P_SP) $ ld d,IX(P_SP+1) $ ld a,(de) $ dec a $ ld (de), a; (*ch->sp)--;
      ; if(*ch->sp
        jp z, 99$
      ; ) {
        ; dec はキャリーフラグを変えないので、255 かどうかは inc a で 0 になるかで見る
        inc a $ jp nz, 96$ ; if(*ch->sp==255) {
          ld (de), a ; (*ch->sp)++; 無限ループのカウンタを 0 に戻す
          ld a,IX(P_DRUM) $ or a $ jp nz, 95$
            ld a,IX(P_NO20) $ out (_IOPortOPLL1), a; ym2413(ch->no20,0)
            xor a $ out	(_IOPortOPLL2), a
            jp 96$
          95$:
            ; ym2413(0x0e,(1<<5)|0);
            ld a,#0x0e $ out (_IOPortOPLL1), a
            ld a,#0x20 $ out (_IOPortOPLL2), a
            xor a
        96$:       ; }
        ld IX(P_SP),e $ ld IX(P_SP+1),d $ dec de
        // dda wait
        ld a,(hl) $ inc hl; u8 a = *ch->pc++;
        ex de,hl $ add a,(hl) $ ex de,hl; a += ch->sp[-1];
        ld e,(hl) ; u8 e = *ch->pc;
        add hl,bc ; ch->pc += bc;
        cp e $ jp c, 98$; if (e <= a) {
          sub a,e; a -= e
          ld e,IX(P_SP) $ dec e $ ld (de),a; ch->sp[-1]=a;
          jp 2$; return;
        98$:; }
        ld e,IX(P_SP) $ dec e $ ld (de),a; ch->sp[-1]=a;
        jp 1$; break;
      99$:; }
      dec de $ dec de $ ld IX(P_SP),e $ ld IX(P_SP+1),d ; ch->sp-=2;
      ld a,(hl) $ inc hl; u8 a = *ch->pc++;
      inc de
      ld c,a $ ld a,(de) $ add a,c; a += ch->sp[1];
      ld c,(hl) $ inc hl; u8 c = *ch->pc++;
      cp c $ jp c, 97$; if (c <= a) {
        sub a,c; a -= c;
        ld(de),a; ch->sp[1]=a;
        jp 2$; return;
      97$:; }
      ld(de),a; ch->sp[1]=a;
      jp 1$; break; 
    10$: ;case PBREAK:
      ; if (*ch->sp == 1
        ld e,IX(P_SP) $ ld d,IX(P_SP+1) $ ld a,(de) $ dec a $ jp nz, 109$
      ; ) {
        ld c,(hl) $ inc hl $ ld b,(hl) $ dec hl; u16 bc = *(u16*)ch->pc;
      101$:
        add hl,bc ; ch->pc += bc;
        dec de $ dec de $ ld IX(P_SP),e $ ld IX(P_SP+1),d; ch->sp-=2;
        ld a,(hl) $ inc hl; u8 a = *ch->pc++;
        inc de
        ld c,a $ ld a,(de) $ add a, c; a += ch->sp[1];
        ld c,(hl) $ inc hl; u8 c = *ch->pc++;
        cp c $ jp c, 107$; if (c <= a) {
          sub a,c; a -= c;
          ld (de), a; ch->sp[1]=a;
          jp 2$; return;
        107$:; }
        ld (de), a; ch->sp[1]=a;
        jp 1$; break;
      109$:; }
      inc hl $ inc hl; ch->pc+=2;
      jp 1$; break;
    17$: ;case PBREAKS: 飛び先を 1 バイト (0〜255) で持つ形
      ld e,IX(P_SP) $ ld d,IX(P_SP+1) $ ld a,(de) $ dec a $ jp nz, 119$; if (*ch->sp == 1) {
        ld c,(hl) $ ld b,#0 ; bc = *ch->pc;
        jp 101$
      119$:; }
      inc hl; ch->pc+=1;
      jp 1$; break;
    11$: ; case PSLOAD:{
        ld a,(hl) $ inc hl ; a = *ch->pc++;
        ; u8* de = &sound[a];
        ex de,hl
        ld	hl, #_sound
        add	a, (hl)
        inc	hl
        ld	c, a
        ld	a, #0x00
        adc	a, (hl)
        ld	h, a
        ld  l, c
        ld bc, #_IOPortOPLL2
        xor a $ out (_IOPortOPLL1), a $ outi; ym2413(0,*de++);
        inc a $ out (_IOPortOPLL1), a $ outi; ym2413(0,*de++);
        inc a $ out (_IOPortOPLL1), a $ outi; ym2413(0,*de++);
        inc a $ out (_IOPortOPLL1), a $ outi; ym2413(0,*de++);
        inc a $ out (_IOPortOPLL1), a $ outi; ym2413(0,*de++);
        inc a $ out (_IOPortOPLL1), a $ outi; ym2413(0,*de++);
        inc a $ out (_IOPortOPLL1), a $ outi; ym2413(0,*de++);
        inc a $ out (_IOPortOPLL1), a $ outi; ym2413(0,*de++);
        ex de,hl
        jp 1$; break;
      ; }
    12$: ; case PDRUM:
      and #0x3f $ ld d,a
      ; ym2413(0x0e,(1<<5)|0);
      ld a,#0x0e $ out (_IOPortOPLL1), a
      ld a,#0x20 $ out (_IOPortOPLL2), a
      ; ym2413(0x0e,(1<<5)|(a&31));
      ld a,#0x0e $ out (_IOPortOPLL1), a
      ld a,d     $ out (_IOPortOPLL2), a
      ld a,(hl) $ inc hl $ ld IX(P_WAIT),a; ch->wait=*ch->pc++
      jp 2$  ; return;
    13$: ; case PSLAON:
      ld IX(P_SLA),#1; ch->sla=1;
      jp 1$ ; break;
    14$: ; case PSUSON:
      ld IX(P_SUS),#0x20; ch->sus=0x20;
      jp 1$ ; break;
    20$: ; case PSUSOFF:
      ld IX(P_SUS),#0; ch->sus=0;
      jp 1$ ; break;
    21$: ; case PTONEF: デチューンした音。音程をデータで持つ
      ld a,IX(P_NO20) $ ld c,a
      xor a $ cp IX(P_SLA) $ jp nz, 211$; if (!ch->sla) {
        ld a,c $ out (_IOPortOPLL1), a
        xor a $ out (_IOPortOPLL2), a
      211$: ; }
      ld IX(P_SLA),#0 ; ch->sla=0
      ld e,l $ ld d,h $ inc hl $ inc hl ; de = 音程の 2 バイト
      jp 32$
    22$: ; case PTONEL: LFO をかける音。スラーでつなぐときは速さのタイマーを続ける
      ld a,IX(P_NO20) $ ld c,a
      xor a $ cp IX(P_SLA) $ jp nz, 221$; if (!ch->sla) {
        ld a,c $ out (_IOPortOPLL1), a
        xor a $ out (_IOPortOPLL2), a
        ld a,IX(P_LA) $ add a,IX(P_LC) $ add a,#2 $ ld IX(P_LT),a ; ch->lt=ch->la+ch->lc+2
      221$: ; }
      ld IX(P_SLA),#0 ; ch->sla=0
      ld IX(P_KEY),#0x10 ; ch->key=0x10
      ld a,(hl) $ ld IX(P_LBASE),a $ inc hl $ ld a,(hl) $ ld IX(P_LBASE+1),a $ dec hl
      xor a $ ld IX(P_LVAL),a $ ld IX(P_LVAL+1),a ; ch->lval=0
      ld a,IX(P_LD) $ ld IX(P_LSTEP),a ; ch->lstep=ch->ld
      ld a,IX(P_LB) $ srl a $ adc a,#0 $ ld IX(P_LCNT),a ; ch->lcnt=(lb+1)>>1
      ld e,l $ ld d,h $ inc hl $ inc hl ; de = 音程の 2 バイト
      jp 32$
    23$: ; case PKEYOFFL:
      ld IX(P_KEY),#0 ; ch->key=0
      jp 4$
    24$: ; case PLFO:
      ld a,(hl) $ inc hl $ ld IX(P_LA),a
      ld a,(hl) $ inc hl $ ld IX(P_LB),a
      srl a $ adc a,#0 $ ld IX(P_LCNT),a ; ch->lcnt=(lb+1)>>1
      ld a,(hl) $ inc hl $ ld IX(P_LC),a
      add a,IX(P_LA) $ add a,#2 $ ld IX(P_LT),a ; ch->lt=ch->la+ch->lc+2
      ld a,(hl) $ inc hl $ ld IX(P_LD),a $ ld IX(P_LSTEP),a
      xor a $ ld IX(P_LVAL),a $ ld IX(P_LVAL+1),a
      ld IX(P_LFO),#1
      ld a,#1 $ ld (_lfo_used),a
      jp 1$
    25$: ; case PLFOOFF:
      ld IX(P_LFO),#0
      jp 1$
    26$: ; case PPORTA: 始めの音程で鳴らし、フレームごとの処理で目標まで動かす
      ld a,IX(P_NO20) $ ld c,a
      xor a $ cp IX(P_SLA) $ jp nz, 261$; if (!ch->sla) {
        ld a,c $ out (_IOPortOPLL1), a
        xor a $ out (_IOPortOPLL2), a
      261$: ; }
      ld IX(P_SLA),#0 ; ch->sla=0
      ld IX(P_KEY),#0x10 ; ch->key=0x10
      ld e,l $ ld d,h ; de = 始めの音程の 2 バイト
      ld a,(hl) $ inc hl $ ld IX(P_PF),a
      ld a,(hl) $ inc hl $ ld b,a $ and #1 $ ld IX(P_PF+1),a
      ld a,b $ srl a $ ld IX(P_PBLK),a
      ld a,(hl) $ inc hl $ ld IX(P_PQ),a
      ld a,(hl) $ inc hl $ ld IX(P_PR),a
      ld a,(hl) $ inc hl $ ld IX(P_PNN),a $ ld IX(P_PN),a
      ld a,(hl) $ inc hl $ ld IX(P_PDIR),a
      ld IX(P_PERR),#0
      ld a,#1 $ ld (_lfo_used),a
      jp 32$
    18$: ; case PDRUMV2: 0x37 と 0x38 に同じ音量を書く
      ld a,#0x37 $ out (_IOPortOPLL1), a
      ld a,(hl) $ inc hl $ out (_IOPortOPLL2), a $ ld c,a
      ld a,#0x38 $ out (_IOPortOPLL1), a
      ld a,c $ out (_IOPortOPLL2), a
      jp 1$ ; break;
    19$: ; case PDRUMV1: 0x37 に音量を書く
      ld a,#0x37 $ out (_IOPortOPLL1), a
      ld a,(hl) $ inc hl $ out (_IOPortOPLL2), a
      jp 1$ ; break;
    15$: ; case PDRUMV:
      ld a,(hl) $ inc hl $ out (_IOPortOPLL1), a ; ym2413(*ch->pc++,*ch->pc++);
      ld a,(hl) $ inc hl $ out (_IOPortOPLL2), a
      jp 1$ ; break;
    ; }
  2$:; }
  ld P_PC(ix),l $ ld P_PC+1(ix),h; (19)(19)=28
  pop ix $ ret
  __endasm;
}
#endif
u8 track_size;
void p_reset(unsigned char mode){
    ym2413(0x0e, mode<<5);
    if (mode) {
      ym2413(0x16, 0x20);// F-Num LSB for channel 7 (slots 13,16)  BD1,BD2
      ym2413(0x17, 0x50);// F-Num LSB for channel 8 (slots 14,17)  HH ,SD
      ym2413(0x18, 0xC0);// F-Num LSB for channel 9 (slots 15,18)　TOM,TCY 
      ym2413(0x26, 0x05);// Block/F-Num MSB for channel 7          BD1,BD2
      ym2413(0x27, 0x05);// Block/F-Num MSB for channel 8          HH ,SD
      ym2413(0x28, 0x01);// Block/F-Num MSB for channel 9          TOM,TCY
      ym2413(0x36, 0xff);
      ym2413(0x37, 0xff);
      ym2413(0x38, 0xff);
    }
}
#ifndef OPT2
void p_play(u8 **bs,u8*stack) {
  p_reset(HDR_MODE(((u8*)bs)[1]));
  u8* sp=stack;
  track_size = (u8)*bs++;
  lfo_used=0;
  sound=*bs++;
  for(u8 i=0;i<track_size;i++) {
    psgdrv[i].pc=bs[i]+1;
    psgdrv[i].wait=1;
    psgdrv[i].tone=0;
    psgdrv[i].no10=i+0x10;
    psgdrv[i].no20=i+0x20;
    psgdrv[i].no30=i+0x30;
    psgdrv[i].sp=sp-1;
    sp += bs[i][0]*2;
    psgdrv[i].sla=0;
    psgdrv[i].sus=0;
    psgdrv[i].lfo=0;
    psgdrv[i].pn=0;
    psgdrv[i].key=0;
    psgdrv[i].drum= (HDR_MODE(((u8*)bs)[-3])!=0 && i==6);
  }
#ifdef PSG
  psg_play(bs+track_size,sp,((u8*)bs)[-3]>>1);
#endif
}
#else
void p_play(u8 **bs,u8* stack) {
  p_reset(HDR_MODE(((u8*)bs)[1]));
  u8* sp=stack;
  PSGDrvCh *p = psgdrv;
  track_size = (u8)*bs++;
  lfo_used=0;
  sound=*bs++;
  for(u8 i=0;i<track_size;i++,p++) {
    p->pc=bs[i]+1;
    p->wait=1;
    p->tone=0;
    p->no10=i+0x10;
    p->no20=i+0x20;
    p->no30=i+0x30;
    p->sp=sp-1;
    sp += bs[i][0]*2;
    p->sla=0;
    p->sus=0;
    p->lfo=0;
    p->pn=0;
    p->key=0;
    p->drum= (HDR_MODE(((u8*)bs)[-3])!=0 && i==6);
  }
#ifdef PSG
  psg_play(bs+track_size,sp,((u8*)bs)[-3]>>1);
#endif
}
#endif
#ifndef OPT
// ソフトウェア LFO を 1 フレーム進める (MGSDRV と同じく、音符の処理より先)
static void p_lfo(PSGDrvCh* ch) {
  if (--ch->lt) return;
  ch->lt=ch->lc+1;
  if (!ch->lcnt) { ch->lstep=-ch->lstep; ch->lcnt=ch->lb+1; } // 三角波の折り返し
  ch->lcnt--;
  ch->lval+=ch->lstep;
  // ずらした F-Number が 172〜344 から出たらブロックをまたぐ (デチューンと同じ)
  s16 f=(ch->lbase&511)+ch->lval;
  u8 blk=ch->lbase>>9;
  while (f<172) { f+=173; blk--; }
  while (f>=345) { f-=173; blk++; }
  u8 t=((blk&7)<<1)|(u8)(f>>8)|ch->sus;
  ch->tone=t;
  ym2413(ch->no10,(u8)f);
  ym2413(ch->no20,t|ch->key);
}
// ポルタメントを 1 フレーム進める。始め + floor(|差| * k / N) を dda で求める (MGSDRV と同じ)
static void p_porta(PSGDrvCh* ch) {
  u16 e=ch->perr+ch->pr;
  u8 d=ch->pq;
  if (e>=ch->pnn) { e-=ch->pnn; d++; }
  ch->perr=(u8)e;
  if (ch->pdir) ch->pf-=d; else ch->pf+=d;
  while (ch->pf<172) { ch->pf+=173; ch->pblk--; }
  while (ch->pf>=345) { ch->pf-=173; ch->pblk++; }
  ch->pblk&=7;
  u8 t=(ch->pblk<<1)|(u8)(ch->pf>>8)|ch->sus;
  ch->tone=t;
  ym2413(ch->no10,(u8)ch->pf);
  ym2413(ch->no20,t|ch->key);
  if (!--ch->pn) {
    // 終わったら、LFO を目標の音程から遅れの分やり直す (ポルタメントの間は LFO を止めている)
    ch->lbase=((u16)ch->pblk<<9)|ch->pf;
    ch->lt=ch->la+ch->lc+2;
    ch->lval=0; ch->lstep=ch->ld; ch->lcnt=(u8)((ch->lb+1)>>1);
  }
}
static void lfo_update(void) {
  PSGDrvCh *p = psgdrv;
  u8 i=track_size;
  do {if (p->pn) p_porta(p); else if (p->lfo) p_lfo(p);p++;} while(--i);
}
#else
// LFO・ポルタメントを 1 フレーム進める (アセンブラ版。C 版と同じことをする)
static void lfo_update(void) __naked {
  __asm
  push ix
  ld ix,#_psgdrv
  ld a,(_track_size) $ ld b,a
  1$: ; do {
    ld a,IX(P_PN) $ or a $ jp nz, 20$ ; if (p->pn) p_porta(p);
    ld a,IX(P_LFO) $ or a $ jp z, 9$ ; else if (p->lfo) p_lfo(p);
    ; p_lfo
    dec IX(P_LT) $ jp nz, 9$ ; if (--ch->lt) return;
    ld a,IX(P_LC) $ inc a $ ld IX(P_LT),a ; ch->lt=ch->lc+1;
    ld a,IX(P_LCNT) $ or a $ jr nz, 2$ ; if (!ch->lcnt) {
      ld a,IX(P_LSTEP) $ neg $ ld IX(P_LSTEP),a ; ch->lstep=-ch->lstep;
      ld a,IX(P_LB) $ inc a ; ch->lcnt=ch->lb+1;
    2$: ; }
    dec a $ ld IX(P_LCNT),a ; ch->lcnt--;
    ld a,IX(P_LSTEP) $ ld e,a $ rla $ sbc a,a $ ld d,a ; de = (s16)ch->lstep
    ld l,IX(P_LVAL) $ ld h,IX(P_LVAL+1) $ add hl,de
    ld IX(P_LVAL),l $ ld IX(P_LVAL+1),h ; ch->lval+=ch->lstep;
    ld e,IX(P_LBASE) $ ld a,IX(P_LBASE+1) $ ld c,a $ and #1 $ ld d,a
    add hl,de ; f=(ch->lbase&511)+ch->lval;
    srl c ; blk=ch->lbase>>9;
    call 50$
    jp 9$
  20$: ; p_porta: 始め + floor(|差| * k / N) を dda で求める
    ld a,IX(P_PERR) $ add a,IX(P_PR) $ ld l,a $ ld a,#0 $ adc a,a $ ld h,a ; e=ch->perr+ch->pr (16 ビット)
    ld d,IX(P_PQ) ; d=ch->pq;
    ld e,IX(P_PNN)
    ld a,h $ or a $ jr nz, 21$ ; if (e>=ch->pnn) {
    ld a,l $ cp e $ jr c, 22$
    21$: ld a,l $ sub e $ ld l,a ; e-=ch->pnn;
      inc d ; d++;
    22$: ; }
    ld IX(P_PERR),l ; ch->perr=e;
    ld e,d $ ld d,#0
    ld l,IX(P_PF) $ ld h,IX(P_PF+1)
    ld a,IX(P_PDIR) $ or a $ jr z, 23$ ; if (ch->pdir) ch->pf-=d; else ch->pf+=d;
      sbc hl,de $ jr 24$ ; or a で キャリーは 0
    23$: add hl,de
    24$:
    ld c,IX(P_PBLK)
    call 50$
    ld IX(P_PF),l $ ld IX(P_PF+1),h
    ld a,c $ and #7 $ ld IX(P_PBLK),a ; ch->pblk&=7;
    dec IX(P_PN) $ jp nz, 9$ ; if (!--ch->pn) {
      ; 終わったら、LFO を目標の音程から遅れの分やり直す
      ld IX(P_LBASE),l $ add a,a $ or h $ ld IX(P_LBASE+1),a ; ch->lbase=(pblk<<9)|pf;
      ld a,IX(P_LA) $ add a,IX(P_LC) $ add a,#2 $ ld IX(P_LT),a ; ch->lt=ch->la+ch->lc+2;
      xor a $ ld IX(P_LVAL),a $ ld IX(P_LVAL+1),a ; ch->lval=0;
      ld a,IX(P_LD) $ ld IX(P_LSTEP),a ; ch->lstep=ch->ld;
      ld a,IX(P_LB) $ srl a $ adc a,#0 $ ld IX(P_LCNT),a ; ch->lcnt=(lb+1)>>1;
    ; }
  9$:
    ld de,#P_SIZE $ add ix,de ; p++
    dec b $ jp nz, 1$ ; } while(--i);
  pop ix
  ret
  50$: ; hl = F-Number (符号付き)、c = ブロック。172〜344 に収めてからレジスタに書く
    bit 7,h $ jr nz, 52$ ; 負なら 172 より小さい
    ld a,h $ or a $ jr nz, 51$
    ld a,l $ cp #172 $ jr c, 52$ ; while (f<172) {
    jr 53$
    51$: ; h>=1
    ld a,h $ dec a $ jr nz, 54$ ; h>=2 なら 345 以上
    ld a,l $ cp #0x59 $ jr c, 55$ ; 0x159 = 345
    54$: ld de,#-173 $ add hl,de $ inc c $ jr 50$ ; f-=173; blk++;
    52$: ld de,#173 $ add hl,de $ dec c $ jr 50$ ; f+=173; blk--;
    53$:
    55$:
    ld a,c $ and #7 $ add a,a $ or h $ or IX(P_SUS) $ ld IX(P_TONE),a ; ch->tone=t;
    ld e,a
    ld a,IX(P_NO10) $ out (_IOPortOPLL1), a
    ld a,l $ out (_IOPortOPLL2), a
    ld a,IX(P_NO20) $ out (_IOPortOPLL1), a
    ld a,e $ or IX(P_KEY) $ out (_IOPortOPLL2), a
    ret
  __endasm;
}
#endif
#ifndef OPT2
void p_update(void) {
#ifdef PSG
  psg_update();
#endif
  if (lfo_used) lfo_update();
  for(u8 i=0;i<track_size;i++) p_exec(&psgdrv[i]);
}
#else
#ifndef OPT3
void p_update(void) {
  PSGDrvCh *p = psgdrv;
#ifdef PSG
  psg_update();
#endif
  if (lfo_used) lfo_update();
  for(u8 i=0;i<track_size;i++,p++) p_exec(p);
}
#else
void p_update(void) {
  PSGDrvCh *p = psgdrv;
  u8 i=track_size;
#ifdef PSG
  psg_update();
#endif
  if (lfo_used) lfo_update();
  do {p_exec(p);p++;} while(--i);
}
#endif
#endif
