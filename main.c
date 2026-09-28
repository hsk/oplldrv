#include "oplldrv.h"
void main(void);
void init(void) {main();}

int putchar(int c) __naked {
	c;
	__asm;
	ld a,l
	out (0), a
	ret
	__endasm;
}
#include SRC
void wait(void) __naked {
	__asm;
    ld a,#0
    out (9), a
	ret
	__endasm;
}
u8 stack[100];
// 演奏するフレーム数 (1/60秒単位)。-D FRAMES=3600 で固定できる (CPU使用率の計測用)
#ifndef FRAMES
#ifndef TAIL
#define TAIL 120 // 曲が終わってから鳴らす余韻
#endif
#define FRAMES (bgm1_frames+TAIL)
#endif
void main(void) {
  p_play(bgm1,stack);
  for (u16 a=0;a<FRAMES;a++) {
    wait();
    p_update();
  }
#ifdef COUNT
  // 命令の回数を "count fm 81 000012AB" の形で出す (0 のものは出さない)
  extern u32 op_count[256], psg_count[256];
  for (u8 k=0;k<2;k++) {
    u32* c = k ? psg_count : op_count;
    for (u16 i=0;i<256;i++) {
      if (!c[i]) continue;
      for (const char* t = k ? "count psg " : "count fm "; *t; t++) putchar(*t);
      u8 n = (u8)i;
      putchar("0123456789ABCDEF"[n>>4]); putchar("0123456789ABCDEF"[n&15]); putchar(' ');
      u32 v = c[i];
      for (s8 b=28;b>=0;b-=4) putchar("0123456789ABCDEF"[(v>>b)&15]);
      putchar('\n');
    }
  }
#endif
}
