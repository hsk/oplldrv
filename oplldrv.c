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
  u8 wait;
  u8* pc;
  u8 tone;
  u8 no10;
  u8 no20;
  u8 no30;
  u8* sp;
} PSGDrvCh;
PSGDrvCh psgdrv[4];
#define P_WAIT 0
#define P_PC   1
#define P_TONE 3
#define P_NO10 4
#define P_NO20 5
#define P_NO30 6
#define P_SP   7
#define P_SIZE 9

#define IX(x) x(ix)

#define PKEYOFF 0
#define PWAIT 1
#define PTONE 2
#define PVOLUME 3
#define PEND 4
#define PLOOP 5
#define PNEXT 6
__sfr __at 0xF0 IOPortOPLL1;
__sfr __at 0xF1 IOPortOPLL2;

#define ym2413(reg,parm) {IOPortOPLL1 = (reg);IOPortOPLL2 = (parm);}
static u16 const tones[] = {
  171,  181,  192,  204,  216,  229,  242,  257,  272,  288,  305,  323,
  683,  693,  704,  716,  728,  741,  754,  769,  784,  800,  817,  835,
 1195, 1205, 1216, 1228, 1240, 1253, 1266, 1281, 1296, 1312, 1329, 1347,
 1707, 1717, 1728, 1740, 1752, 1765, 1778, 1793, 1808, 1824, 1841, 1859,
 2219, 2229, 2240, 2252, 2264, 2277, 2290, 2305, 2320, 2336, 2353, 2371,
 2731, 2741, 2752, 2764, 2776, 2789, 2802, 2817, 2832, 2848, 2865, 2883,
 3243, 3253, 3264, 3276, 3288, 3301, 3314, 3329, 3344, 3360, 3377, 3395,
 3755, 3765, 3776, 3788, 3800, 3813, 3826, 3841, 3856, 3872, 3889, 3907,
};

#ifndef OPT
void p_exec(PSGDrvCh* ch) {
  if (--ch->wait) {
    return;
  }
  ch->wait++;
  while (1) {
    u8 a = *ch->pc++;
    switch (a) {
    case PKEYOFF: ym2413(ch->no20,ch->tone);
    case PWAIT: a=*ch->pc++;ch->wait=a; return;
    case PTONE:
      ym2413(ch->no20,0);
      a=*ch->pc++; a=a+a;
      u8* iy = &((u8*)tones)[a];
      a = iy[0];
      ym2413(ch->no10,a);
      a = iy[1];
      ch->tone=a; 
      ym2413(ch->no20,(1<<4)|a); break;
    case PVOLUME: a=*ch->pc++; ym2413(ch->no30, a); break;
    case PEND:  ch->pc--; ch->wait=0; return;
    case PLOOP: a = *ch->pc++; ch->sp++; *ch->sp = a; break;
    case PNEXT: (*ch->sp)--;
                if(*ch->sp) {
                  u16 de = *(u16*)ch->pc;
                  ch->pc += de;
                  break;
                }
                ch->sp--; ch->pc += 2; break; 
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
      cp #PWAIT $ jp c,3$ $ jp z,4$
      cp #PVOLUME $ jp c,5$ $ jp z,6$
      cp #PLOOP $ jp c,7$ $ jp z,8$ $ jp 9$
    ; ) {
    3$:; case PKEYOFF:
      ; ym2413(0x20+ch->no,ch->tone);
      ld a,IX(P_NO20) $ out (_IOPortOPLL1), a
      ld a,IX(P_TONE) $ out	(_IOPortOPLL2), a
    4$:; case PWAIT:
      ld a,(hl) $ inc hl $ ld IX(P_WAIT),a; ch->wait=*ch->pc++
      jp 2$; return;
    5$:; case PTONE:
      ; ym2413(0x20+ch->no,0);
      ld a,IX(P_NO20) $ ld c,a $ out (_IOPortOPLL1), a
      xor a $ out (_IOPortOPLL2), a
      ; a=*ch->pc++; a=a+a;
      ld a,(hl) $ inc hl $ add a,a
      ; u8* iy = &((u8*)tones)[a];
      add a, #<(_tones) $ ld e, a $ ld a, #0x00 $ adc a, #>(_tones) $ ld d, a
      //ld de,#_tones $ add e $ ld e,a $ jr nc, 55$ $ inc d $ 55$:
      ; a = iy[0];
      ; ym2413(0x10+ch->no,a);
        ld a,IX(P_NO10) $ out (_IOPortOPLL1), a
      ld a,(de) $ inc de $ out (_IOPortOPLL2), a
      ; a = iy[1];
      ; ch->tone=a;
      ; ym2413(0x20+ch->no,(1<<4)|(a));
      ld a,c $ out (_IOPortOPLL1), a
      ld a,(de) $ ld IX(P_TONE), a $ or a, #16 $ out (_IOPortOPLL2), a
      inc hl $ ld a,(hl) $ inc hl $ ld IX(P_WAIT),a 
      jp 2$; break;
    6$:; case PVOLUME:
      ; ym2413(0x30+ch->no,*ch->pc++);
      ld a,IX(P_NO30) $ out (_IOPortOPLL1), a
      ld a,(hl) $ inc hl $ out (_IOPortOPLL2), a
      jp 1$; break;
    7$: dec hl $ ld IX(P_WAIT),#0 ; case PEND:  ch->pc--;
      jp 2$  ; return;
    8$: ; case PLOOP:
      ld a,(hl) $ inc hl ; a = *ch->pc++;
      inc IX(P_SP); ch->sp++;
      ld e,IX(P_SP) $ ld d,IX(P_SP+1) $ ld (de), a; *ch->sp = a;
      jp 1$; break;
    9$: ;case PNEXT:
      // ld e,IX(P_SP) $ ld d,IX(P_SP+1) $ ex de,hl $ dec (hl) $ ex de,hl; (*ch->sp)--;
      ld e,IX(P_SP) $ ld d,IX(P_SP+1) $ ld a,(de) $ dec a $ ld (de), a; (*ch->sp)--;
      ; if(*ch->sp
        jp z, 99$
      ; ) {
        ld e,(hl) $ inc hl $ ld d,(hl) $ dec hl; u16 de = *(u16*)ch->pc;
        add hl,de ; ch->pc += de;
        jp 1$; break;
      99$:; }
      dec IX(P_SP); ch->sp--;
      inc hl $ inc hl; ch->pc += 2;
      jp 1$; break; 

    ; }
  2$:; }
  ld P_PC(ix),l $ ld P_PC+1(ix),h
  pop ix $ ret
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
u8 stack[100];
#ifndef OPT2
void p_play(u8 **bs) {
  u8* sp=stack;
  for(int i=0;i<4;i++) {
    psgdrv[i].pc=bs[i]+1;
    psgdrv[i].wait=1;
    psgdrv[i].tone=0;
    psgdrv[i].no10=i+0x10;
    psgdrv[i].no20=i+0x20;
    psgdrv[i].no30=i+0x30;
    psgdrv[i].sp=sp-1;
    sp += bs[i][0];
  }
}
#else
void p_play(u8 **bs) {
  u8* sp=stack;
  PSGDrvCh *p = psgdrv;
  for(u8 i=0;i<4;i++,p++) {
    p->pc=bs[i]+1;
    p->wait=1;
    p->tone=0;
    p->no10=i+0x10;
    p->no20=i+0x20;
    p->no30=i+0x30;
    p->sp=sp-1;
    sp += bs[i][0];
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
