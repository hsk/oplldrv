#include "oplldrv.h"
//#define OPT 1
//#define OPT2 1 
//#ifdef DEVKITSMS

u8* sound;
PSGDrvCh psgdrv[9];
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
    case PEND:  ch->pc--; ch->wait=0;ym2413(ch->no20,0); return;
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
      cp #PTONEF $ jp c,20$ $ jp 21$
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
  p_reset(((u8*)bs)[1]);
  u8* sp=stack;
  track_size = (u8)*bs++;
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
    psgdrv[i].drum= (((u8*)bs)[-3]!=0 && i==6);
  }
}
#else
void p_play(u8 **bs,u8* stack) {
  p_reset(((u8*)bs)[1]);
  u8* sp=stack;
  PSGDrvCh *p = psgdrv;
  track_size = (u8)*bs++;
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
    p->drum= (((u8*)bs)[-3]!=0 && i==6);
  }
}
#endif
#ifndef OPT2
void p_update(void) {
  for(u8 i=0;i<track_size;i++) p_exec(&psgdrv[i]);
}
#else
#ifndef OPT3
void p_update(void) {
  PSGDrvCh *p = psgdrv;
  for(u8 i=0;i<track_size;i++,p++) p_exec(p);
}
#else
void p_update(void) {
  PSGDrvCh *p = psgdrv;
  u8 i=track_size;
  do {p_exec(p);p++;} while(--i);
}
#endif
#endif
