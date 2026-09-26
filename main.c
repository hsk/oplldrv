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
}
