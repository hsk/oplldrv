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
  u8 sus;
  u8 drum;
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
#define P_SIZE 12
#define IX(x) x(ix)

#define PDRUM   0x60
#define PKEYOFF 0x80
#define PWAIT   0x81
#define PVOLUME 0x82
#define PEND    0x83
#define PLOOP   0x84
#define PNEXT   0x85
#define PBREAK  0x86
#define PSLOAD  0x87
#define PSLAON  0x88
#define PSUSON  0x89
#define PDRUMV  0x8A

void p_play(u8 **bs,u8* stack);
void p_update(void);
unsigned char p_init(void);
