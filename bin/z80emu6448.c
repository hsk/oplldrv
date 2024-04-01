/*
64*48のフルカラーマシン
*/
#include "z80emu.h"
#include "emu2149.h"
#include "emu2413.h"
#include "emu76489.h"
//s64 baseClk = 3993600;//  PC-6001
//s64 MSX_CLK = 3579545;
const s64 MSX_CLK = 44100*81;

z80reg_t reg;
static u8 s_mem[0x10000];
static u8 ReadMem(u16 addr) {
	return s_mem[addr];
}
static void WriteMem(u16 addr, u8 data) {
	s_mem[addr] = data;
}
static u8 ReadIO(u16 port) {
	return 0;
}
static u8 vram_addr_p;
static u16 vram_addr;
static u8 vram[64*64];
static u16 palette_addr;
static u8 palette[256*3];
static void vdp_init() {
	memset(vram,0,sizeof(vram));
	vram_addr=vram_addr_p=1;
	memset(palette,0,sizeof(palette));
	palette_addr=0;
}
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
void wav_header(FILE* fp) {
  char header[46];
  #define DATALENGTH 0
  chunkID(header, "RIFF");
  DWORD(header + 4, DATALENGTH * 2 + 36);
  chunkID(header + 8, "WAVE");
  chunkID(header + 12, "fmt ");
  DWORD(header + 16, 16);
  WORD(header + 20, 1);               /* WAVE_FORMAT_PCM */
  WORD(header + 22, 1);               /* channel 1=mono,2=stereo */
  DWORD(header + 24, 44100);     /* samplesPerSec */
  DWORD(header + 28, 2 * 44100); /* bytesPerSec */
  WORD(header + 32, 2);               /* blockSize */
  WORD(header + 34, 16);              /* bitsPerSample */
  chunkID(header + 36, "data");
  DWORD(header + 40, 2 * DATALENGTH);
  fwrite(header,46,1,fp);
}
void wav_close(FILE* fp) {
	char dt[4];
	u32 len = ftell(fp);
	fseek(fp,4,SEEK_SET);
	DWORD(dt,len*2+36);
	fwrite(dt,4,1,fp);
	fseek(fp,40,SEEK_SET);
	DWORD(dt,len*2);
	fwrite(dt,4,1,fp);
	fseek(fp,0,SEEK_END);
	fclose(fp);
}
u64 wait_clk;
PSG *psg =NULL;
u8 psg_reg;
s64 psg_clk;
FILE *psgf;
void psg_init(){
	if (psg!=NULL) return;
	psg = PSG_new(MSX_CLK/2,44100);
	PSG_setVolumeMode(psg, 2); // AY style
	PSG_set_quality(psg,1);
	PSG_reset(psg);

	psg_clk = 0;
	psgf = fopen("psg.wav","wb");
	wav_header(psgf);
}
void psg_update(){
	if (psg==NULL) return;
	while (reg.clk - psg_clk >= 81) {
		u16 b = PSG_calc(psg);
		fprintf(psgf,"%c%c",b&255,b>>8);
		psg_clk += 81;
	}
}
void psg_close() {
	if (psg==NULL) return;
	wav_close(psgf);
	PSG_delete(psg);
	psg = NULL;
}
SNG *sng =NULL;
u8 sng_reg;
s64 sng_clk;
FILE *sngf;
void sng_init(){
	if (sng!=NULL) return;
	sng = SNG_new(MSX_CLK,44100);
	SNG_set_quality(sng,1);
	SNG_reset(sng);

	sng_clk = 0;
	sngf = fopen("sng.wav","wb");
	wav_header(sngf);

}
void sng_update(){
	if (sng==NULL) return;
	while (reg.clk - sng_clk >= 81) {
		u16 b = SNG_calc(sng);
		fprintf(sngf,"%c%c",b&255,b>>8);
		sng_clk += 81;
	}
}
void sng_close() {
	if (sng==NULL) return;
	wav_close(sngf);
	SNG_delete(sng);
	sng = NULL;
}
OPLL *opll =NULL;
u8 opll_reg;
s64 opll_clk;
FILE *opllf;
void opll_init(){
	if (opll!=NULL) return;
	opll = OPLL_new(MSX_CLK,44100);
	OPLL_set_quality(opll,1);
	OPLL_reset(opll);

	opll_clk = 0;
	opllf = fopen("opll.wav","wb");
	wav_header(opllf);

}
#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))
void opll_update(){
	if (opll==NULL) return;
	while (reg.clk - opll_clk >= 81) {
		//u16 b = (u16)OPLL_calc(opll);
		int b1 = (((int)OPLL_calc(opll))*2);
		if (b1 < -32768) b1=-32768;
		if (b1 > 32767) b1=32767;
		u16 b=b1;
		fprintf(opllf,"%c%c",b&255,b>>8);
		opll_clk += 81;
	}
}
void opll_close() {
	if (opll==NULL) return;
	wav_close(opllf);
	OPLL_delete(opll);
	opll = NULL;
}

u64 skip_clk=0;
static void WriteIO(u16 port, u8 data) {
	//printf("port %d %d\n",port&0xff,data);
	switch(port&0xff){
		case 0: printf("%c", data); break;
		case 1: fprintf(stderr, "%c", data); break;
		case 2: ((u8*)&vram_addr)[vram_addr_p] = data; vram_addr_p=1-vram_addr_p; break;
		case 3: vram_addr_p=1;vram[vram_addr&0xfff]=data;vram_addr++; break; 
		case 4: palette_addr = data*3; break;
		case 5:
			palette[palette_addr]=data;palette_addr++;
			if(palette_addr==256*3)palette_addr=0;
			break; 
		case 6:
			for (int i=0;i<64*48;i++) {
				printf("%c%c%c",palette[vram[i]*3],palette[vram[i]*3+1],palette[vram[i]*3+2]);
			}
			break;
		case 7:
			printf("output\n");
			for (int i=0;i<256;i++) {
				printf("pal %3d %02x%02x%02x\n",i,palette[i*3],palette[i*3+1],palette[i*3+2]);
			}
			for (int i=0, y=0;y<48;y++) {
				for (int x=0;x<64;x++,i++) {
					printf("%02x ",vram[i]);
				}
				printf("\n");
			}
			for (int i=0, y=0;y<48;y++) {
				for (int x=0;x<64;x++,i++) {
					u16 v =((u16)vram[i])*3;
					printf("%02x %02x%02x%02x ",v, palette[v],palette[v+1],palette[v+2]);
				}
				printf("\n");
			}
			break;
		case 8:
			printf("%d\n", data);
			break;
		case 9:
			while(wait_clk<=reg.clk) wait_clk += (44100*81/60);
			skip_clk += wait_clk-reg.clk;
			reg.clk = wait_clk;
			break;
		case 0x7e:// sms psg
			sng_init();
			SNG_writeIO(sng,data);
			break;
		case 0xa0: // msx psg
			psg_reg=data;
			break;
		case 0xa1: 
			psg_init();
			PSG_writeReg(psg,psg_reg,data);
			break;
		case 0x7c:// msx fm
		case 0xf0:// sms fm
			printf("opll0 %d\n",data);
			opll_init();
			OPLL_writeIO(opll,0,data);
			break;
		case 0x7d:
		case 0xf1:
			printf("opll1 %d\n",data);
			OPLL_writeIO(opll,1,data);
			break;
	}
}
int main(int argc, char *argv[]) {
	if (argc != 2) {
		fprintf(stderr, "usage : z80emu <program.z80>\n");
		exit(1);
	}

	memset(s_mem, 0, 0x10000);
	vdp_init();
	FILE *fp = fopen(argv[1], "rb");
	if (fp == NULL) {
		fprintf(stderr, "Can't open %s\n", argv[1]);
		exit(1);
	}
	fseek(fp, 0, SEEK_END);
	size_t size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	u16 baseAddr = fgetc(fp);
	baseAddr |= fgetc(fp) << 8;
	size -= ftell(fp);
	fread(&s_mem[baseAddr], 1, size, fp);
	fclose(fp);
	InitZ80();
	memset(&reg, 0, sizeof(reg));
	reg.SP = 0xfffc;
	s_mem[0xfffc] = 0xfe;
	s_mem[0xfffd] = 0xff;
	s_mem[0xfffe] = 0x76;
	reg.PC = baseAddr;
	reg.clk = wait_clk = 0;
	for(;;) {
		reg.R++;
		u8 inst = ReadMemI(&reg);
		if (inst == 0x76) {
			break;
		}
		(*instTbl[inst])(&reg);
		psg_update();
		sng_update();
		opll_update();
	}
	psg_close();
	sng_close();
	opll_close();
	for(int i = 0; i < 4; i++) {
		s64 clk;
		s32 sec, hh, mm, ss;
		if (i == 0) {
			clk = reg.clk - skip_clk;
		} else {
			clk = reg.counter[i - 1];
		}
		if (clk == 0) {
			continue;
		}
		s64 microsec = ((clk)*1000000L / MSX_CLK) % 1000000L;
		sec =  (clk / MSX_CLK);
		hh = sec / 3600;
		mm = (sec - hh * 3600) / 60;
		ss = sec % 60;
		if (i) {
			fprintf(stderr, "counter %d ; ", i);
		}
		fprintf(stderr, "%lld clk (%d sec., %d:%02d:%02d.%08lld) skip %lld clk %0.3f%%\n",
			(long long)clk, sec, hh, mm, ss,microsec,skip_clk,(double)(reg.clk-skip_clk)*100/reg.clk);
	}
	return 0;
}
