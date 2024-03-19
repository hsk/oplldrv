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
void main(void) {
  p_play(bgm1,stack);
  for (u16 a=0;a<60*60;a++) {
    wait();
    p_update();
  }
}
