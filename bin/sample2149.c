/*============================================================

  Test code for emu2149.c

    Write 2 seconds of the piano tone into sample2149.wav

  gcc -Wall -lm sample2149.c emu2149.c
  (The author had tried to compile on Solaris7 with gcc 2.8.1)

=============================================================*/
#include "emu2149.h"
#include <stdio.h>
#include <time.h>

/*
 * Standard clock = MSX clock
 */
#define MSX_CLK 3579545

#define SAMPLERATE 44100
#define DATALENGTH (SAMPLERATE * 2)

static void WORD(char *buf, uint32_t data) {

  buf[0] = data & 0xff;
  buf[1] = (data & 0xff00) >> 8;
}

static void DWORD(char *buf, uint32_t data) {

  buf[0] = data & 0xff;
  buf[1] = (data & 0xff00) >> 8;
  buf[2] = (data & 0xff0000) >> 16;
  buf[3] = (data & 0xff000000) >> 24;
}

static void chunkID(char *buf, char id[4]) {

  buf[0] = id[0];
  buf[1] = id[1];
  buf[2] = id[2];
  buf[3] = id[3];
}

int main(void) {

  static char wave[DATALENGTH * 2];
  char filename[16] = "sample2149.wav";
  char header[46];
  int i;
  clock_t start, finish;

  FILE *fp;
  PSG *psg;

  /*
   * Create WAVE header
   */
  chunkID(header, "RIFF");
  DWORD(header + 4, DATALENGTH * 2 + 36);
  chunkID(header + 8, "WAVE");
  chunkID(header + 12, "fmt ");
  DWORD(header + 16, 16);
  WORD(header + 20, 1);               /* WAVE_FORMAT_PCM */
  WORD(header + 22, 1);               /* channel 1=mono,2=stereo */
  DWORD(header + 24, SAMPLERATE);     /* samplesPerSec */
  DWORD(header + 28, 2 * SAMPLERATE); /* bytesPerSec */
  WORD(header + 32, 2);               /* blockSize */
  WORD(header + 34, 16);              /* bitsPerSample */
  chunkID(header + 36, "data");
  DWORD(header + 40, 2 * DATALENGTH);

  psg = PSG_new(MSX_CLK/2, SAMPLERATE);
  PSG_setVolumeMode(psg, 2); // AY style
  PSG_setQuality(psg,1);
  PSG_reset(psg);

  //CH-A Freq
	PSG_writeReg(psg,0,0x55);
	PSG_writeReg(psg,1,0);
	//Mixer
	PSG_writeReg(psg,7,56);
	//Vol
	PSG_writeReg(psg,8,15);

  start = clock();

  i = 0;
  int16_t v = 0;
  for (i = 0; i < DATALENGTH; i++) {
    if (i==DATALENGTH/2) PSG_writeReg(psg,0,0xAA);
    WORD(wave + i * 2, PSG_calc(psg));
  }

  finish = clock();
  PSG_delete(psg);

  printf("It has been %f sec to calc %d waves.\n", (double)(finish - start) / CLOCKS_PER_SEC, DATALENGTH);
  printf("%f times faster than real YM2413.\n",
         ((double)DATALENGTH / SAMPLERATE) / ((double)(finish - start) / CLOCKS_PER_SEC));

  fp = fopen(filename, "wb");

  if (fp == NULL)
    return 1;

  fwrite(header, 46, 1, fp);
  fwrite(wave, DATALENGTH, 2, fp);

  fclose(fp);

  printf("Wrote : %s\n", filename);

  return 0;
}
