#include <stdio.h>
void main(void);
void init(void) {main();}

int putchar(int c) __naked {
	c;
	__asm;
	ld a,l
	out (0), a
end$:
	ret
	__endasm;
}

typedef char s8;
typedef short s16;
typedef unsigned char u8;
typedef unsigned short u16;
typedef volatile s8 vs8;
typedef volatile s16 vs16;
typedef volatile u8 vu8;
typedef volatile u16 vu16;
typedef struct PSGDrvCh {
  u8* pc;
  u8 wait;
  u8 tone;
  u8 no;
} PSGDrvCh;
PSGDrvCh psgdrv[4];
#define P_PC   0
#define P_WAIT 2
#define P_TONE 3
#define P_NO 4
#define P_SIZE 5
#define IX(x) x(ix)

#define PKEYOFF 0
#define PWAIT 1
#define PTONE 2
#define PVOLUME 3
#define PEND 4

__sfr __at 0xF0 IOPortOPLL1;
__sfr __at 0xF1 IOPortOPLL2;

#define ym2413(reg,parm) {IOPortOPLL1 = (reg);IOPortOPLL2 = (parm);}

#ifndef OPT
void p_exec(PSGDrvCh* ch) {
  if (ch->wait) {
    ch->wait--;
    return;
  }
  while (1) {
    u8 a = *ch->pc++;
    switch (a) {
    case PKEYOFF: ym2413(0x20+ch->no,ch->tone);
    case PWAIT: a=*ch->pc++;ch->wait=a; return;
    case PTONE:
      ym2413(0x20+ch->no,0);
      a=*ch->pc++;
      ym2413(0x10+ch->no,a);
      a=*ch->pc++;
      ch->tone=a; 
      ym2413(0x20+ch->no,(1<<4)|a); break;
    case PVOLUME: a=*ch->pc++; ym2413(0x30+ch->no, a); break;
    case PEND:  ch->pc--; return;
    }
  }
}
#else
#define $ __endasm;__asm
void p_exec(PSGDrvCh* ch) __naked {
  ch;
  __asm
  push ix
  push hl $ pop ix
  ld l,(ix) $ ld h,1(ix)
  ; if (ch->wait
    ld a,IX(P_WAIT) $ or a $ jp z,1$
  ; ) {
    dec a $ ld IX(P_WAIT),a; ch->wait--;
    jp 2$; return;
  1$:; }
  ; while (1) {
    ld a,(hl) $ inc hl; u8 a = *ch->pc++;
    ; switch (a
      cp #PWAIT $ jp c,3$ $ jp z,4$
      cp #PVOLUME $ jp c,5$ $ jp z,6$ $ jp 7$
    ; ) {
    3$:; case PKEYOFF:
      ; ym2413(0x20+ch->no,ch->tone);
      ld a,IX(P_NO) $ add a,#0x20 $ out (_IOPortOPLL1),a
      ld a,IX(P_TONE) $ out	(_IOPortOPLL2), a
    4$:; case PWAIT:
      ld a,(hl) $ inc hl $ ld IX(P_WAIT),a; ch->wait=*ch->pc++
      jp 2$; return;
    5$:; case PTONE:
      ; ym2413(0x20+ch->no,0);
      ld a,IX(P_NO) $ add a,#0x20 $ out (_IOPortOPLL1), a
      xor a $ out (_IOPortOPLL2), a
      ; ym2413(0x10+ch->no,*ch->pc++);
      ld a,IX(P_NO) $ add a,#0x10 $ out (_IOPortOPLL1), a
      ld a,(hl) $ inc hl          $ out (_IOPortOPLL2), a
      ld a,(hl) $ inc hl $ ld IX(P_TONE), a ; ch->tone=*ch->pc++;
      ; ym2413(0x20+ch->no,(1<<4)|a)
      push af
      ld a,IX(P_NO) $ add a,#0x20 $ out (_IOPortOPLL1), a
      pop af $ or a, #16 $ out (_IOPortOPLL2), a
      jp 1$; break;
    6$:; case PVOLUME:
      ; ym2413(0x30+ch->no,*ch->pc++);
      ld a,IX(P_NO) $ add a,#0x30 $ out (_IOPortOPLL1), a
      ld a,(hl) $ inc hl $ out (_IOPortOPLL2), a
      jp 1$; break;
    7$: dec hl; case PEND:  ch->pc--;
      ; jp 2$  ; return;
    ; }
  2$:; }
  ld (ix),l $ ld 1(ix),h
  pop ix
  ret
  __endasm;
}
#endif
#include "bgm1.h"
void wait(void) __naked {
	__asm;
    ld a,#0
    out (9), a
	ret
	__endasm;
}
#ifndef OPT2
void p_play(u8 **bs) {
  for(int i=0;i<4;i++) {
    psgdrv[i].pc=bs[i];
    psgdrv[i].wait=0;
    psgdrv[i].no=i;
    psgdrv[i].tone=0;
  }
}
#else
void p_play(u8 **bs) {
  PSGDrvCh *p = psgdrv;
  for(u8 i=0;i<4;i++,p++) {
    p->pc=bs[i];
    p->wait=0;
    p->no=i;
    p->tone=0;
  }
}
#endif
#ifndef OPT2
void p_update(void) {
  for(int i=0;i<4;i++) p_exec(&psgdrv[i]);
}
#else
#ifndef OPT3
void p_update(void) {
  PSGDrvCh *p = psgdrv;
  for(u8 i=0;i<4;i++,p++) p_exec(p);
}
#else
void p_update(void) {
  PSGDrvCh *p = psgdrv;
  u8 i=4;
  do {p_exec(p);p++;} while(--i);
}
#endif
#endif
void main(void) {
  p_play(bgm1);
  for (u16 a=0;a<60*16;a++) {
    wait();
    p_update();
  }
}
