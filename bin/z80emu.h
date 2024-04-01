/* Z80 Emulator (simplified) Version 1.00
     Programmed by XEVI  '19/05/01 */

#define USE_COUNTER		1
#define Z80_BIG_ENDIAN	0

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef signed char		s8;
typedef unsigned char	u8;
typedef short			s16;
typedef unsigned short	u16;
typedef int				s32;
typedef unsigned int	u32;
typedef int64_t			s64;
typedef uint64_t		u64;

enum {
	FS	= 0x80,
	FZ	= 0x40,
	FH	= 0x10,
	FPV	= 0x04,
	FN	= 0x02,
	FC	= 0x01,
};

#if (Z80_BIG_ENDIAN != 0)

typedef union {
	struct {
		u8 H;
		u8 L;
	} B;
	u16 HL;
} r16;

#else

typedef union {
	struct {
		u8 L;
		u8 H;
	} B;
	u16 HL;
} r16;

#endif

typedef struct {
	u16 PC, SP;
	r16 BC, DE, HL, AF;
	r16 BCD, DED, HLD, AFD;
	r16 IX, IY;
	u8 I, R, R_MSB, IFF, IM;

	s64 clk;
	s64 counter[3];
	s64 startClk[3];
} z80reg_t;

#define ADD_CLK(x) { pr->clk += x; }

#define OP_INC(x) { x++; rf = (rf & FC) | s_incTbl[x]; }
#define OP_DEC(x) { x--; rf = (rf & FC) | s_decTbl[x]; }

#define OP_ADD(x, y) { rf = s_addTbl[((y) << 8) + x]; x += y; }
#define OP_ADC(x, y) { u8 tc; tc = x + y + (rf & FC); \
	rf = s_addTbl[((rf & FC) << 16) + ((y) << 8) + x]; x = tc; }
#define OP_SUB(x, y) rf = s_subTbl[((y) << 8) + x]; x -= y;
#define OP_SBC(x, y) { u8 tc; tc = x - y - (rf & FC); \
	rf = s_subTbl[((rf & FC) << 16) + ((y) << 8) + x]; x = tc; }
#define OP_CP(x, y) { rf = s_subTbl[((y) << 8) + x]; }

#define OP_RLC(x) { u8 tc; tc = x; x = (x << 1) | (x >> 7); \
	rf = s_szpTbl[x] | (tc >> 7); }
#define OP_RRC(x) { u8 tc; tc = x; x = (x >> 1) | (x << 7); \
	rf = s_szpTbl[x] | (tc & FC); }
#define OP_RL(x) { u8 tc; tc = x; x = (x << 1) | (rf & FC); \
	rf = s_szpTbl[x] | (tc >> 7); }
#define OP_RR(x) { u8 tc; tc = x; x = (x >> 1) | (rf << 7); \
	rf = s_szpTbl[x] | (tc & FC); }
#define OP_SLA(x) { u8 tc; tc = x; x <<= 1; \
	rf = s_szpTbl[x] | (tc >> 7); }
#define OP_SRA(x) { u8 tc; tc = x; x = (s8)x >> 1; \
	rf = s_szpTbl[x] | (tc & FC); }
#define OP_SRL(x) { u8 tc; tc = x; x >>= 1; \
	rf = s_szpTbl[x] | (tc & FC); }

static u8 s_incTbl[0x100], s_decTbl[0x100], s_szpTbl[0x100];
static u8 s_daaATbl[0x800], s_daaFTbl[0x800];
static u8 s_addTbl[0x20000], s_subTbl[0x20000];

static void ErrExit(const char *msg)
{
	fprintf(stderr, "%s\n", msg);
	exit(1);
}

static u8 ReadMem(u16 addr);
static void WriteMem(u16 addr, u8 data);
static u8 ReadIO(u16 addr);
static void WriteIO(u16 addr, u8 data);

static void PrintReg(z80reg_t *pr) {
	printf("PC =%04X  SP =%04X  (PC)=%02X  (PC+1)=%02X\n",
	  pr->PC, pr->SP, ReadMem(pr->PC), ReadMem(pr->PC + 1));
	printf("BC =%04X  DE =%04X  HL =%04X  AF =%04X\n",
	  pr->BC.HL, pr->DE.HL, pr->HL.HL, pr->AF.HL);
	printf("BC'=%04X  DE'=%04X  HL'=%04X  AF'=%04X\n",
	  pr->BCD.HL, pr->DED.HL, pr->HLD.HL, pr->AFD.HL);
	printf("IX =%04X  IY =%04X  I=%02X  R=%02X  IFF=%d  IM=%d\n",
	  pr->IX.HL, pr->IY.HL, pr->I, (pr->R_MSB & 0x80) | (pr->R & 0x7f),
	  pr->IFF, pr->IM);
	fflush(stdout);
	return;
}

static u8 ReadMemI(z80reg_t *pr)
{
	return ReadMem(pr->PC++);
}

static u16 ReadMemI16(z80reg_t *pr)
{
	u16 d = ReadMem(pr->PC++);
	d |= ReadMem(pr->PC++) << 8;
	return d;
}

static u16 ReadMem16(u16 addr)
{
	u16 d = ReadMem(addr);
	d |= ReadMem((u16)(addr + 1)) << 8;
	return d;
}

static void WriteMem16(u16 addr, u16 data)
{
	WriteMem(addr, (u8)data);
	WriteMem((u16)(addr + 1), (u8)(data >> 8));
	return;
}

#define rb   (pr->BC.B.H)
#define rc   (pr->BC.B.L)
#define rd   (pr->DE.B.H)
#define re   (pr->DE.B.L)
#define rh   (pr->HL.B.H)
#define rl   (pr->HL.B.L)
#define ra   (pr->AF.B.H)
#define rf   (pr->AF.B.L)
#define rpc  (pr->PC)
#define rsp  (pr->SP)
#define rbc  (pr->BC.HL)
#define rde  (pr->DE.HL)
#define rhl  (pr->HL.HL)
#define raf  (pr->AF.HL)

/* NOP */
static void i_00(z80reg_t *pr)
{
	ADD_CLK(4)
	return;
}

/* LD BC,nn */
static void i_01(z80reg_t *pr)
{
	rbc = ReadMemI16(pr);
	ADD_CLK(10)
	return;
}

/* LD (BC),A */
static void i_02(z80reg_t *pr)
{
	WriteMem(rbc, ra);
	ADD_CLK(7)
	return;
}

/* INC BC */
static void i_03(z80reg_t *pr)
{
	rbc++;
	ADD_CLK(6)
	return;
}

/* INC B */
static void i_04(z80reg_t *pr)
{
	OP_INC(rb)
	ADD_CLK(4)
	return;
}

/* DEC B */
static void i_05(z80reg_t *pr)
{
	OP_DEC(rb)
	ADD_CLK(4)
	return;
}

/* LD B,n */
static void i_06(z80reg_t *pr)
{
	rb = ReadMemI(pr);
	ADD_CLK(7)
	return;
}

/* RLCA */
static void i_07(z80reg_t *pr)
{
	u8 c = ra;
	ra = (ra << 1) | (c >> 7);
	rf &= ~(FH | FN | FC);
	rf |= c >> 7;
	ADD_CLK(4)
	return;
}

/* EX AF,AF' */
static void i_08(z80reg_t *pr)
{
	u16 s = raf;
	raf = pr->AFD.HL;
	pr->AFD.HL = s;
	ADD_CLK(4)
	return;
}

/* ADD HL,BC */
static void i_09(z80reg_t *pr)
{
	u32 l = (u32)rhl + (u32)rbc;
	rhl = (u16)l;
	rf &= ~(FN | FC);
	if ((l & 0x10000) != 0) {
		rf |= FC;
	}
	ADD_CLK(11)
	return;
}

/* LD A,(BC) */
static void i_0a(z80reg_t *pr)
{
	ra = ReadMem(rbc);
	ADD_CLK(7)
	return;
}

/* DEC BC */
static void i_0b(z80reg_t *pr)
{
	rbc--;
	ADD_CLK(6)
	return;
}

/* INC C */
static void i_0c(z80reg_t *pr)
{
	OP_INC(rc)
	ADD_CLK(4)
	return;
}

/* DEC C */
static void i_0d(z80reg_t *pr)
{
	OP_DEC(rc)
	ADD_CLK(4)
	return;
}

/* LD C,n */
static void i_0e(z80reg_t *pr)
{
	rc = ReadMemI(pr);
	ADD_CLK(6)
	return;
}

/* RRCA */
static void i_0f(z80reg_t *pr)
{
	u8 c = ra;
	ra = (ra >> 1) | (c << 7);
	rf &= ~(FH | FN | FC);
	rf |= c & FC;
	ADD_CLK(4)
	return;
}

/* DJNZ e */
static void i_10(z80reg_t *pr)
{
	s8 ofst = ReadMemI(pr);
	rb--;
	if (rb != 0) {
		rpc += ofst;
		ADD_CLK(13)
	} else {
		ADD_CLK(8)
	}
	return;
}

/* LD DE,nn */
static void i_11(z80reg_t *pr)
{
	rde = ReadMemI16(pr);
	ADD_CLK(10)
	return;
}

/* LD (DE),A */
static void i_12(z80reg_t *pr)
{
	WriteMem(rde, ra);
	ADD_CLK(7)
	return;
}

/* INC DE */
static void i_13(z80reg_t *pr)
{
	rde++;
	ADD_CLK(6)
	return;
}

/* INC D */
static void i_14(z80reg_t *pr)
{
	OP_INC(rd)
	ADD_CLK(4)
	return;
}

/* DEC D */
static void i_15(z80reg_t *pr)
{
	OP_DEC(rd)
	ADD_CLK(4)
	return;
}

/* LD D,n */
static void i_16(z80reg_t *pr)
{
	rd = ReadMemI(pr);
	ADD_CLK(7)
	return;
}

/* RLA */
static void i_17(z80reg_t *pr)
{
	u8 c = ra;
	ra = (ra << 1) | (rf & FC);
	rf &= ~(FH | FN | FC);
	rf |= (c >> 7);
	ADD_CLK(4)
	return;
}

/* JR e */
static void i_18(z80reg_t *pr)
{
	s8 ofst = ReadMemI(pr);
	rpc += ofst;
	ADD_CLK(12)
	return;
}

/* ADD HL,DE */
static void i_19(z80reg_t *pr)
{
	u32 l = (u32)rhl + (u32)rde;
	rhl = (u16)l;
	rf &= ~(FN | FC);
	if ((l & 0x10000) != 0) {
		rf |= FC;
	}
	ADD_CLK(11)
	return;
}

/* LD A,(DE) */
static void i_1a(z80reg_t *pr)
{
	ra = ReadMem(rde);
	ADD_CLK(7)
	return;
}

/* DEC DE */
static void i_1b(z80reg_t *pr)
{
	rde--;
	ADD_CLK(6)
	return;
}

/* INC E */
static void i_1c(z80reg_t *pr)
{
	OP_INC(re)
	ADD_CLK(4)
	return;
}

/* DEC E */
static void i_1d(z80reg_t *pr)
{
	OP_DEC(re)
	ADD_CLK(4)
	return;
}

/* LD E,n */
static void i_1e(z80reg_t *pr)
{
	re = ReadMemI(pr);
	ADD_CLK(7)
	return;
}

/* RRA */
static void i_1f(z80reg_t *pr)
{
	u8 c = ra;
	ra = (ra >> 1) | (rf << 7);
	rf &= ~(FH | FN | FC);
	rf |= c & FC;
	ADD_CLK(4)
	return;
}

/* JR NZ,e */
static void i_20(z80reg_t *pr)
{
	s8 ofst = ReadMemI(pr);
	if ((rf & FZ) == 0) {
		rpc += ofst;
		ADD_CLK(12)
	} else {
		ADD_CLK(7)
	}
	return;
}

/* LD HL,nn */
static void i_21(z80reg_t *pr)
{
	rhl = ReadMemI16(pr);
	ADD_CLK(10)
	return;
}

/* LD (nn),HL */
static void i_22(z80reg_t *pr)
{
	u16 m = ReadMemI16(pr);
	WriteMem16(m, rhl);
	ADD_CLK(16)
	return;
}

/* INC HL */
static void i_23(z80reg_t *pr)
{
	rhl++;
	ADD_CLK(6)
	return;
}

/* INC H */
static void i_24(z80reg_t *pr)
{
	OP_INC(rh)
	ADD_CLK(4)
	return;
}

/* DEC H */
static void i_25(z80reg_t *pr)
{
	OP_DEC(rh)
	ADD_CLK(4)
	return;
}

/* LD H,n */
static void i_26(z80reg_t *pr)
{
	rh = ReadMemI(pr);
	ADD_CLK(7)
	return;
}

/* DAA */
static void i_27(z80reg_t *pr)
{
	u8 c = (rf & (FN | FC)) | ((rf & FH) >> 2);
	u32 idx = (c << 8) + ra;
	rf = s_daaFTbl[idx];
	ra = s_daaATbl[idx];
	ADD_CLK(4)
	return;
}

/* JR Z,e */
static void i_28(z80reg_t *pr)
{
	s8 ofst = ReadMemI(pr);
	if ((rf & FZ) != 0) {
		rpc += ofst;
		ADD_CLK(12)
	} else {
		ADD_CLK(7)
	}
	return;
}

/* ADD HL,HL */
static void i_29(z80reg_t *pr)
{
	rf &= ~(FN | FC);
	if ((rhl & 0x8000) != 0) {
		rf |= FC;
	}
	rhl += rhl;
	ADD_CLK(11)
	return;
}

/* LD HL,(nn) */
static void i_2a(z80reg_t *pr)
{
	u16 m = ReadMemI16(pr);
	rhl = ReadMem16(m);
	ADD_CLK(16)
	return;
}

/* DEC HL */
static void i_2b(z80reg_t *pr)
{
	rhl--;
	ADD_CLK(6)
	return;
}

/* INC L */
static void i_2c(z80reg_t *pr)
{
	OP_INC(rl)
	ADD_CLK(4)
	return;
}

/* DEC L */
static void i_2d(z80reg_t *pr)
{
	OP_DEC(rl)
	ADD_CLK(4)
	return;
}

/* LD L,n */
static void i_2e(z80reg_t *pr)
{
	rl = ReadMemI(pr);
	ADD_CLK(7)
	return;
}

/* CPL */
static void i_2f(z80reg_t *pr)
{
	ra = ~ra;
	rf |= FH | FN;
	ADD_CLK(4)
	return;
}

/* JR NC,e */
static void i_30(z80reg_t *pr)
{
	s8 ofst = ReadMemI(pr);
	if ((rf & FC) == 0) {
		rpc += ofst;
		ADD_CLK(12)
	} else {
		ADD_CLK(7)
	}
	return;
}

/* LD SP,nn */
static void i_31(z80reg_t *pr)
{
	rsp = ReadMemI16(pr);
	ADD_CLK(10)
	return;
}

/* LD (nn),A */
static void i_32(z80reg_t *pr)
{
	u16 m = ReadMemI16(pr);
	WriteMem(m, ra);
	ADD_CLK(13)
	return;
}

/* INC SP */
static void i_33(z80reg_t *pr)
{
	rsp++;
	ADD_CLK(6)
	return;
}

/* INC (HL) */
static void i_34(z80reg_t *pr)
{
	u8 c = ReadMem(rhl);
	OP_INC(c)
	WriteMem(rhl, c);
	ADD_CLK(11)
	return;
}

/* DEC (HL) */
static void i_35(z80reg_t *pr)
{
	u8 c = ReadMem(rhl);
	OP_DEC(c)
	WriteMem(rhl, c);
	ADD_CLK(11)
	return;
}

/* LD (HL),n */
static void i_36(z80reg_t *pr)
{
	WriteMem(rhl, ReadMemI(pr));
	ADD_CLK(10)
	return;
}

/* SCF */
static void i_37(z80reg_t *pr)
{
	rf &= ~(FH | FN);
	rf |= FC;
	ADD_CLK(4)
	return;
}

/* JR C,e */
static void i_38(z80reg_t *pr)
{
	s8 ofst = ReadMemI(pr);
	if ((rf & FC) != 0) {
		rpc += ofst;
		ADD_CLK(12)
	} else {
		ADD_CLK(7)
	}
	return;
}

/* ADD HL,SP */
static void i_39(z80reg_t *pr)
{
	u32 l = (u32)rhl + (u32)rsp;
	rhl = (u16)l;
	rf &= ~(FN | FC);
	if ((l & 0x10000) != 0) {
		rf |= FC;
	}
	ADD_CLK(11)
	return;
}

/* LD A,(nn) */
static void i_3a(z80reg_t *pr)
{
	u16 m = ReadMemI16(pr);
	ra = ReadMem(m);
	ADD_CLK(13)
	return;
}

/* DEC SP */
static void i_3b(z80reg_t *pr)
{
	rsp--;
	ADD_CLK(6)
	return;
}

/* INC A */
static void i_3c(z80reg_t *pr)
{
	OP_INC(ra)
	ADD_CLK(4)
	return;
}

/* DEC A */
static void i_3d(z80reg_t *pr)
{
	OP_DEC(ra)
	ADD_CLK(4)
	return;
}

/* LD A,n */
static void i_3e(z80reg_t *pr)
{
	ra = ReadMemI(pr);
	ADD_CLK(7)
	return;
}

/* CCF */
static void i_3f(z80reg_t *pr)
{
	rf &= ~FN;
	rf ^= FC;
	ADD_CLK(4)
	return;
}

/* LD B,B */
static void i_40(z80reg_t *pr)
{
#if (USE_COUNTER != 0)
	pr->startClk[0] = pr->clk;
#else
	ADD_CLK(4)
#endif
	return;
}

/* LD B,C */
static void i_41(z80reg_t *pr)
{
	rb = rc;
	ADD_CLK(4)
	return;
}

/* LD B,D */
static void i_42(z80reg_t *pr)
{
	rb = rd;
	ADD_CLK(4)
	return;
}

/* LD B,E */
static void i_43(z80reg_t *pr)
{
	rb = re;
	ADD_CLK(4)
	return;
}

/* LD B,H */
static void i_44(z80reg_t *pr)
{
	rb = rh;
	ADD_CLK(4)
	return;
}

/* LD B,L */
static void i_45(z80reg_t *pr)
{
	rb = rl;
	ADD_CLK(4)
	return;
}

/* LD B,(HL) */
static void i_46(z80reg_t *pr)
{
	rb = ReadMem(rhl);
	ADD_CLK(7)
	return;
}

/* LD B,A */
static void i_47(z80reg_t *pr)
{
	rb = ra;
	ADD_CLK(4)
	return;
}

/* LD C,B */
static void i_48(z80reg_t *pr)
{
	rc = rb;
	ADD_CLK(4)
	return;
}

/* LD C,C */
static void i_49(z80reg_t *pr)
{
#if (USE_COUNTER != 0)
	pr->counter[0] += pr->clk - pr->startClk[0];
#else
	ADD_CLK(4)
#endif
	return;
}

/* LD C,D */
static void i_4a(z80reg_t *pr)
{
	rc = rd;
	ADD_CLK(4)
	return;
}

/* LD C,E */
static void i_4b(z80reg_t *pr)
{
	rc = re;
	ADD_CLK(4)
	return;
}

/* LD C,H */
static void i_4c(z80reg_t *pr)
{
	rc = rh;
	ADD_CLK(4)
	return;
}

/* LD C,L */
static void i_4d(z80reg_t *pr)
{
	rc = rl;
	ADD_CLK(4)
	return;
}

/* LD C,(HL) */
static void i_4e(z80reg_t *pr)
{
	rc = ReadMem(rhl);
	ADD_CLK(7)
	return;
}

/* LD C,A */
static void i_4f(z80reg_t *pr)
{
	rc = ra;
	ADD_CLK(4)
	return;
}

/* LD D,B */
static void i_50(z80reg_t *pr)
{
	rd = rb;
	ADD_CLK(4)
	return;
}

/* LD D,C */
static void i_51(z80reg_t *pr)
{
	rd = rc;
	ADD_CLK(4)
	return;
}

/* LD D,D */
static void i_52(z80reg_t *pr)
{
#if (USE_COUNTER != 0)
	pr->startClk[1] = pr->clk;
#else
	ADD_CLK(4)
#endif
	return;
}

/* LD D,E */
static void i_53(z80reg_t *pr)
{
	rd = re;
	ADD_CLK(4)
	return;
}

/* LD D,H */
static void i_54(z80reg_t *pr)
{
	rd = rh;
	ADD_CLK(4)
	return;
}

/* LD D,L */
static void i_55(z80reg_t *pr)
{
	rd = rl;
	ADD_CLK(4)
	return;
}

/* LD D,(HL) */
static void i_56(z80reg_t *pr)
{
	rd = ReadMem(rhl);
	ADD_CLK(7)
	return;
}

/* LD D,A */
static void i_57(z80reg_t *pr)
{
	rd = ra;
	ADD_CLK(4)
	return;
}

/* LD E,B */
static void i_58(z80reg_t *pr)
{
	re = rb;
	ADD_CLK(4)
	return;
}

/* LD E,C */
static void i_59(z80reg_t *pr)
{
	re = rc;
	ADD_CLK(4)
	return;
}

/* LD E,D */
static void i_5a(z80reg_t *pr)
{
	re = rd;
	ADD_CLK(4)
	return;
}

/* LD E,E */
static void i_5b(z80reg_t *pr)
{
#if (USE_COUNTER != 0)
	pr->counter[1] += pr->clk - pr->startClk[1];
#else
	ADD_CLK(4)
#endif
	return;
}

/* LD E,H */
static void i_5c(z80reg_t *pr)
{
	re = rh;
	ADD_CLK(4)
	return;
}

/* LD E,L */
static void i_5d(z80reg_t *pr)
{
	re = rl;
	ADD_CLK(4)
	return;
}

/* LD E,(HL) */
static void i_5e(z80reg_t *pr)
{
	re = ReadMem(rhl);
	ADD_CLK(7)
	return;
}

/* LD E,A */
static void i_5f(z80reg_t *pr)
{
	re = ra;
	ADD_CLK(4)
	return;
}

/* LD H,B */
static void i_60(z80reg_t *pr)
{
	rh = rb;
	ADD_CLK(4)
	return;
}

/* LD H,C */
static void i_61(z80reg_t *pr)
{
	rh = rc;
	ADD_CLK(4)
	return;
}

/* LD H,D */
static void i_62(z80reg_t *pr)
{
	rh = rd;
	ADD_CLK(4)
	return;
}

/* LD H,E */
static void i_63(z80reg_t *pr)
{
	rh = re;
	ADD_CLK(4)
	return;
}

/* LD H,H */
static void i_64(z80reg_t *pr)
{
#if (USE_COUNTER != 0)
	pr->startClk[2] = pr->clk;
#else
	ADD_CLK(4)
#endif
	return;
}

/* LD H,L */
static void i_65(z80reg_t *pr)
{
	rh = rl;
	ADD_CLK(4)
	return;
}

/* LD H,(HL) */
static void i_66(z80reg_t *pr)
{
	rh = ReadMem(rhl);
	ADD_CLK(7)
	return;
}

/* LD H,A */
static void i_67(z80reg_t *pr)
{
	rh = ra;
	ADD_CLK(4)
	return;
}

/* LD L,B */
static void i_68(z80reg_t *pr)
{
	rl = rb;
	ADD_CLK(4)
	return;
}

/* LD L,C */
static void i_69(z80reg_t *pr)
{
	rl = rc;
	ADD_CLK(4)
	return;
}

/* LD L,D */
static void i_6a(z80reg_t *pr)
{
	rl = rd;
	ADD_CLK(4)
	return;
}

/* LD L,E */
static void i_6b(z80reg_t *pr)
{
	rl = re;
	ADD_CLK(4)
	return;
}

/* LD L,H */
static void i_6c(z80reg_t *pr)
{
	rl = rh;
	ADD_CLK(4)
	return;
}

/* LD L,L */
static void i_6d(z80reg_t *pr)
{
#if (USE_COUNTER != 0)
	pr->counter[2] += pr->clk - pr->startClk[2];
#else
	ADD_CLK(4)
#endif
	return;
}

/* LD L,(HL) */
static void i_6e(z80reg_t *pr)
{
	rl = ReadMem(rhl);
	ADD_CLK(7)
	return;
}

/* LD L,A */
static void i_6f(z80reg_t *pr)
{
	rl = ra;
	ADD_CLK(4)
	return;
}

/* LD (HL),B */
static void i_70(z80reg_t *pr)
{
	WriteMem(rhl, rb);
	ADD_CLK(7)
	return;
}

/* LD (HL),C */
static void i_71(z80reg_t *pr)
{
	WriteMem(rhl, rc);
	ADD_CLK(7)
	return;
}

/* LD (HL),D */
static void i_72(z80reg_t *pr)
{
	WriteMem(rhl, rd);
	ADD_CLK(7)
	return;
}

/* LD (HL),E */
static void i_73(z80reg_t *pr)
{
	WriteMem(rhl, re);
	ADD_CLK(7)
	return;
}

/* LD (HL),H */
static void i_74(z80reg_t *pr)
{
	WriteMem(rhl, rh);
	ADD_CLK(7)
	return;
}

/* LD (HL),L */
static void i_75(z80reg_t *pr)
{
	WriteMem(rhl, rl);
	ADD_CLK(7)
	return;
}

/* HALT */
static void i_76(z80reg_t *pr)
{
	PrintReg(pr);
	ErrExit("HALT.");
	return;
}

/* LD (HL),A */
static void i_77(z80reg_t *pr)
{
	WriteMem(rhl, ra);
	ADD_CLK(7)
	return;
}

/* LD A,B */
static void i_78(z80reg_t *pr)
{
	ra = rb;
	ADD_CLK(4)
	return;
}

/* LD A,C */
static void i_79(z80reg_t *pr)
{
	ra = rc;
	ADD_CLK(4)
	return;
}

/* LD A,D */
static void i_7a(z80reg_t *pr)
{
	ra = rd;
	ADD_CLK(4)
	return;
}

/* LD A,E */
static void i_7b(z80reg_t *pr)
{
	ra = re;
	ADD_CLK(4)
	return;
}

/* LD A,H */
static void i_7c(z80reg_t *pr)
{
	ra = rh;
	ADD_CLK(4)
	return;
}

/* LD A,L */
static void i_7d(z80reg_t *pr)
{
	ra = rl;
	ADD_CLK(4)
	return;
}

/* LD A,(HL) */
static void i_7e(z80reg_t *pr)
{
	ra = ReadMem(rhl);
	ADD_CLK(7)
	return;
}

/* LD A,A */
static void i_7f(z80reg_t *pr)
{
	ADD_CLK(4)
	return;
}

/* ADD A,B */
static void i_80(z80reg_t *pr)
{
	OP_ADD(ra, rb)
	ADD_CLK(4)
	return;
}

/* ADD A,C */
static void i_81(z80reg_t *pr)
{
	OP_ADD(ra, rc)
	ADD_CLK(4)
	return;
}

/* ADD A,D */
static void i_82(z80reg_t *pr)
{
	OP_ADD(ra, rd)
	ADD_CLK(4)
	return;
}

/* ADD A,E */
static void i_83(z80reg_t *pr)
{
	OP_ADD(ra, re)
	ADD_CLK(4)
	return;
}

/* ADD A,H */
static void i_84(z80reg_t *pr)
{
	OP_ADD(ra, rh)
	ADD_CLK(4)
	return;
}

/* ADD A,L */
static void i_85(z80reg_t *pr)
{
	OP_ADD(ra, rl)
	ADD_CLK(4)
	return;
}

/* ADD A,(HL) */
static void i_86(z80reg_t *pr)
{
	u8 c = ReadMem(rhl);
	OP_ADD(ra, c)
	ADD_CLK(7)
	return;
}

/* ADD A,A */
static void i_87(z80reg_t *pr)
{
	OP_ADD(ra, ra)
	ADD_CLK(4)
	return;
}

/* ADC A,B */
static void i_88(z80reg_t *pr)
{
	OP_ADC(ra, rb)
	ADD_CLK(4)
	return;
}

/* ADC A,C */
static void i_89(z80reg_t *pr)
{
	OP_ADC(ra, rc)
	ADD_CLK(4)
	return;
}

/* ADC A,D */
static void i_8a(z80reg_t *pr)
{
	OP_ADC(ra, rd)
	ADD_CLK(4)
	return;
}

/* ADC A,E */
static void i_8b(z80reg_t *pr)
{
	OP_ADC(ra, re)
	ADD_CLK(4)
	return;
}

/* ADC A,H */
static void i_8c(z80reg_t *pr)
{
	OP_ADC(ra, rh)
	ADD_CLK(4)
	return;
}

/* ADC A,L */
static void i_8d(z80reg_t *pr)
{
	OP_ADC(ra, rl)
	ADD_CLK(4)
	return;
}

/* ADC A,(HL) */
static void i_8e(z80reg_t *pr)
{
	u8 c = ReadMem(rhl);
	OP_ADC(ra, c)
	ADD_CLK(7)
	return;
}

/* ADC A,A */
static void i_8f(z80reg_t *pr)
{
	OP_ADC(ra, ra)
	ADD_CLK(4)
	return;
}

/* SUB B */
static void i_90(z80reg_t *pr)
{
	OP_SUB(ra, rb)
	ADD_CLK(4)
	return;
}

/* SUB C */
static void i_91(z80reg_t *pr)
{
	OP_SUB(ra, rc)
	ADD_CLK(4)
	return;
}

/* SUB D */
static void i_92(z80reg_t *pr)
{
	OP_SUB(ra, rd)
	ADD_CLK(4)
	return;
}

/* SUB E */
static void i_93(z80reg_t *pr)
{
	OP_SUB(ra, re)
	ADD_CLK(4)
	return;
}

/* SUB H */
static void i_94(z80reg_t *pr)
{
	OP_SUB(ra, rh)
	ADD_CLK(4)
	return;
}

/* SUB L */
static void i_95(z80reg_t *pr)
{
	OP_SUB(ra, rl)
	ADD_CLK(4)
	return;
}

/* SUB (HL) */
static void i_96(z80reg_t *pr)
{
	u8 c = ReadMem(rhl);
	OP_SUB(ra, c)
	ADD_CLK(7)
	return;
}

/* SUB A */
static void i_97(z80reg_t *pr)
{
	OP_SUB(ra, ra)
	ADD_CLK(4)
	return;
}

/* SBC A,B */
static void i_98(z80reg_t *pr)
{
	OP_SBC(ra, rb)
	ADD_CLK(4)
	return;
}

/* SBC A,C */
static void i_99(z80reg_t *pr)
{
	OP_SBC(ra, rc)
	ADD_CLK(4)
	return;
}

/* SBC A,D */
static void i_9a(z80reg_t *pr)
{
	OP_SBC(ra, rd)
	ADD_CLK(4)
	return;
}

/* SBC A,E */
static void i_9b(z80reg_t *pr)
{
	OP_SBC(ra, re)
	ADD_CLK(4)
	return;
}

/* SBC A,H */
static void i_9c(z80reg_t *pr)
{
	OP_SBC(ra, rh)
	ADD_CLK(4)
	return;
}

/* SBC A,L */
static void i_9d(z80reg_t *pr)
{
	OP_SBC(ra, rl)
	ADD_CLK(4)
	return;
}

/* SBC A,(HL) */
static void i_9e(z80reg_t *pr)
{
	u8 c = ReadMem(rhl);
	OP_SBC(ra, c)
	ADD_CLK(7)
	return;
}

/* SBC A,A */
static void i_9f(z80reg_t *pr)
{
	OP_SBC(ra, ra)
	ADD_CLK(4)
	return;
}

/* AND B */
static void i_a0(z80reg_t *pr)
{
	ra &= rb;
	rf = s_szpTbl[ra] | FH;
	ADD_CLK(4)
	return;
}

/* AND C */
static void i_a1(z80reg_t *pr)
{
	ra &= rc;
	rf = s_szpTbl[ra] | FH;
	ADD_CLK(4)
	return;
}

/* AND D */
static void i_a2(z80reg_t *pr)
{
	ra &= rd;
	rf = s_szpTbl[ra] | FH;
	ADD_CLK(4)
	return;
}

/* AND E */
static void i_a3(z80reg_t *pr)
{
	ra &= re;
	rf = s_szpTbl[ra] | FH;
	ADD_CLK(4)
	return;
}

/* AND H */
static void i_a4(z80reg_t *pr)
{
	ra &= rh;
	rf = s_szpTbl[ra] | FH;
	ADD_CLK(4)
	return;
}

/* AND L */
static void i_a5(z80reg_t *pr)
{
	ra &= rl;
	rf = s_szpTbl[ra] | FH;
	ADD_CLK(4)
	return;
}

/* AND (HL) */
static void i_a6(z80reg_t *pr)
{
	ra &= ReadMem(rhl);
	rf = s_szpTbl[ra] | FH;
	ADD_CLK(7)
	return;
}

/* AND A */
static void i_a7(z80reg_t *pr)
{
	rf = s_szpTbl[ra] | FH;
	ADD_CLK(4)
	return;
}

/* XOR B */
static void i_a8(z80reg_t *pr)
{
	ra ^= rb;
	rf = s_szpTbl[ra];
	ADD_CLK(4)
	return;
}

/* XOR C */
static void i_a9(z80reg_t *pr)
{
	ra ^= rc;
	rf = s_szpTbl[ra];
	ADD_CLK(4)
	return;
}

/* XOR D */
static void i_aa(z80reg_t *pr)
{
	ra ^= rd;
	rf = s_szpTbl[ra];
	ADD_CLK(4)
	return;
}

/* XOR E */
static void i_ab(z80reg_t *pr)
{
	ra ^= re;
	rf = s_szpTbl[ra];
	ADD_CLK(4)
	return;
}

/* XOR H */
static void i_ac(z80reg_t *pr)
{
	ra ^= rh;
	rf = s_szpTbl[ra];
	ADD_CLK(4)
	return;
}

/* XOR L */
static void i_ad(z80reg_t *pr)
{
	ra ^= rl;
	rf = s_szpTbl[ra];
	ADD_CLK(4)
	return;
}

/* XOR (HL) */
static void i_ae(z80reg_t *pr)
{
	ra ^= ReadMem(rhl);
	rf = s_szpTbl[ra];
	ADD_CLK(7)
	return;
}

/* XOR A */
static void i_af(z80reg_t *pr)
{
	ra = 0;
	rf = s_szpTbl[ra];
	ADD_CLK(4)
	return;
}

/* OR B */
static void i_b0(z80reg_t *pr)
{
	ra |= rb;
	rf = s_szpTbl[ra];
	ADD_CLK(4)
	return;
}

/* OR C */
static void i_b1(z80reg_t *pr)
{
	ra |= rc;
	rf = s_szpTbl[ra];
	ADD_CLK(4)
	return;
}

/* OR D */
static void i_b2(z80reg_t *pr)
{
	ra |= rd;
	rf = s_szpTbl[ra];
	ADD_CLK(4)
	return;
}

/* OR E */
static void i_b3(z80reg_t *pr)
{
	ra |= re;
	rf = s_szpTbl[ra];
	ADD_CLK(4)
	return;
}

/* OR H */
static void i_b4(z80reg_t *pr)
{
	ra |= rh;
	rf = s_szpTbl[ra];
	ADD_CLK(4)
	return;
}

/* OR L */
static void i_b5(z80reg_t *pr)
{
	ra |= rl;
	rf = s_szpTbl[ra];
	ADD_CLK(4)
	return;
}

/* OR (HL) */
static void i_b6(z80reg_t *pr)
{
	ra |= ReadMem(rhl);
	rf = s_szpTbl[ra];
	ADD_CLK(7)
	return;
}

/* OR A */
static void i_b7(z80reg_t *pr)
{
	rf = s_szpTbl[ra];
	ADD_CLK(4)
	return;
}

/* CP B */
static void i_b8(z80reg_t *pr)
{
	OP_CP(ra, rb)
	ADD_CLK(4)
	return;
}

/* CP C */
static void i_b9(z80reg_t *pr)
{
	OP_CP(ra, rc)
	ADD_CLK(4)
	return;
}

/* CP D */
static void i_ba(z80reg_t *pr)
{
	OP_CP(ra, rd)
	ADD_CLK(4)
	return;
}

/* CP E */
static void i_bb(z80reg_t *pr)
{
	OP_CP(ra, re)
	ADD_CLK(4)
	return;
}

/* CP H */
static void i_bc(z80reg_t *pr)
{
	OP_CP(ra, rh)
	ADD_CLK(4)
	return;
}

/* CP L */
static void i_bd(z80reg_t *pr)
{
	OP_CP(ra, rl)
	ADD_CLK(4)
	return;
}

/* CP (HL) */
static void i_be(z80reg_t *pr)
{
	u8 c = ReadMem(rhl);
	OP_CP(ra, c)
	ADD_CLK(7)
	return;
}

/* CP A */
static void i_bf(z80reg_t *pr)
{
	OP_CP(ra, ra)
	ADD_CLK(4)
	return;
}

/* RET NZ */
static void i_c0(z80reg_t *pr)
{
	if ((rf & FZ) == 0) {
		rpc = ReadMem16(rsp);
		rsp += 2;
		ADD_CLK(11)
	} else {
		ADD_CLK(5)
	}
	return;
}

/* POP BC */
static void i_c1(z80reg_t *pr)
{
	rbc = ReadMem16(rsp);
	rsp += 2;
	ADD_CLK(10)
	return;
}

/* JP NZ,nn */
static void i_c2(z80reg_t *pr)
{
	u16 m = ReadMemI16(pr);
	if ((rf & FZ) == 0) {
		rpc = m;
	}
	ADD_CLK(10)
	return;
}

/* JP nn */
static void i_c3(z80reg_t *pr)
{
	rpc = ReadMemI16(pr);
	ADD_CLK(10)
	return;
}

/* CALL NZ,nn */
static void i_c4(z80reg_t *pr)
{
	u16 m = ReadMemI16(pr);
	if ((rf & FZ) == 0) {
		rsp -= 2;
		WriteMem16(rsp, rpc);
		rpc = m;
		ADD_CLK(17)
	} else {
		ADD_CLK(10)
	}
	return;
}

/* PUSH BC */
static void i_c5(z80reg_t *pr)
{
	rsp -= 2;
	WriteMem16(rsp, rbc);
	ADD_CLK(11)
	return;
}

/* ADD A,n */
static void i_c6(z80reg_t *pr)
{
	u8 c = ReadMemI(pr);
	OP_ADD(ra, c)
	ADD_CLK(7)
	return;
}

/* RST 00H */
static void i_c7(z80reg_t *pr)
{
	rsp -= 2;
	WriteMem16(rsp, rpc);
	rpc = 0x0000;
	ADD_CLK(11)
	return;
}

/* RET Z */
static void i_c8(z80reg_t *pr)
{
	if ((rf & FZ) != 0) {
		rpc = ReadMem16(rsp);
		rsp += 2;
		ADD_CLK(11)
	} else {
		ADD_CLK(5)
	}
	return;
}

/* RET */
static void i_c9(z80reg_t *pr)
{
	rpc = ReadMem16(rsp);
	rsp += 2;
	ADD_CLK(10)
	return;
}

/* JP Z,nn */
static void i_ca(z80reg_t *pr)
{
	u16 m = ReadMemI16(pr);
	if ((rf & FZ) != 0) {
		rpc = m;
	}
	ADD_CLK(10)
	return;
}

/* CB xx */
static void i_cb(z80reg_t *pr)
{
	pr->R++;
	u8 cbn = ReadMemI(pr);
	u8 bitp = 1 << ((cbn >> 3) & 0x07);
	switch(cbn) {
		case 0x00: /* RLC B */
			OP_RLC(rb)
			break;

		case 0x01: /* RLC C */
			OP_RLC(rc)
			break;

		case 0x02: /* RLC D */
			OP_RLC(rd)
			break;

		case 0x03: /* RLC E */
			OP_RLC(re)
			break;

		case 0x04: /* RLC H */
			OP_RLC(rh)
			break;

		case 0x05: /* RLC L */
			OP_RLC(rl)
			break;

		case 0x06: /* RLC (HL) */
		{
			u8 c = ReadMem(rhl);
			OP_RLC(c)
			WriteMem(rhl, c);
			ADD_CLK(7)
			break;
		}

		case 0x07: /* RLC A */
			OP_RLC(ra)
			break;

		case 0x08: /* RRC B */
			OP_RRC(rb)
			break;

		case 0x09: /* RRC C */
			OP_RRC(rc)
			break;

		case 0x0a: /* RRC D */
			OP_RRC(rd)
			break;

		case 0x0b: /* RRC E */
			OP_RRC(re)
			break;

		case 0x0c: /* RRC H */
			OP_RRC(rh)
			break;

		case 0x0d: /* RRC L */
			OP_RRC(rl)
			break;

		case 0x0e: /* RRC (HL) */
		{
			u8 c = ReadMem(rhl);
			OP_RRC(c)
			WriteMem(rhl, c);
			ADD_CLK(7)
			break;
		}

		case 0x0f: /* RRC A */
			OP_RRC(ra)
			break;

		case 0x10: /* RL B */
			OP_RL(rb)
			break;

		case 0x11: /* RL C */
			OP_RL(rc)
			break;

		case 0x12: /* RL D */
			OP_RL(rd)
			break;

		case 0x13: /* RL E */
			OP_RL(re)
			break;

		case 0x14: /* RL H */
			OP_RL(rh)
			break;

		case 0x15: /* RL L */
			OP_RL(rl)
			break;

		case 0x16: /* RL (HL) */
		{
			u8 c = ReadMem(rhl);
			OP_RL(c)
			WriteMem(rhl, c);
			ADD_CLK(7)
			break;
		}

		case 0x17: /* RL A */
			OP_RL(ra)
			break;

		case 0x18: /* RR B */
			OP_RR(rb)
			break;

		case 0x19: /* RR C */
			OP_RR(rc)
			break;

		case 0x1a: /* RR D */
			OP_RR(rd)
			break;

		case 0x1b: /* RR E */
			OP_RR(re)
			break;

		case 0x1c: /* RR H */
			OP_RR(rh)
			break;

		case 0x1d: /* RR L */
			OP_RR(rl)
			break;

		case 0x1e: /* RR (HL) */
		{
			u8 c = ReadMem(rhl);
			OP_RR(c)
			WriteMem(rhl, c);
			ADD_CLK(7)
			break;
		}

		case 0x1f: /* RR A */
			OP_RR(ra)
			break;

		case 0x20: /* SLA B */
			OP_SLA(rb)
			break;

		case 0x21: /* SLA C */
			OP_SLA(rc)
			break;

		case 0x22: /* SLA D */
			OP_SLA(rd)
			break;

		case 0x23: /* SLA E */
			OP_SLA(re)
			break;

		case 0x24: /* SLA H */
			OP_SLA(rh)
			break;

		case 0x25: /* SLA L */
			OP_SLA(rl)
			break;

		case 0x26: /* SLA (HL) */
		{
			u8 c = ReadMem(rhl);
			OP_SLA(c)
			WriteMem(rhl, c);
			ADD_CLK(7)
			break;
		}

		case 0x27: /* SLA A */
			OP_SLA(ra)
			break;

		case 0x28: /* SRA B */
			OP_SRA(rb)
			break;

		case 0x29: /* SRA C */
			OP_SRA(rc)
			break;

		case 0x2a: /* SRA D */
			OP_SRA(rd)
			break;

		case 0x2b: /* SRA E */
			OP_SRA(re)
			break;

		case 0x2c: /* SRA H */
			OP_SRA(rh)
			break;

		case 0x2d: /* SRA L */
			OP_SRA(rl)
			break;

		case 0x2e: /* SRA (HL) */
		{
			u8 c = ReadMem(rhl);
			OP_SRA(c)
			WriteMem(rhl, c);
			ADD_CLK(7)
			break;
		}

		case 0x2f: /* SRA A */
			OP_SRA(ra)
			break;

		case 0x38: /* SRL B */
			OP_SRL(rb)
			break;

		case 0x39: /* SRL C */
			OP_SRL(rc)
			break;

		case 0x3a: /* SRL D */
			OP_SRL(rd)
			break;

		case 0x3b: /* SRL E */
			OP_SRL(re)
			break;

		case 0x3c: /* SRL H */
			OP_SRL(rh)
			break;

		case 0x3d: /* SRL L */
			OP_SRL(rl)
			break;

		case 0x3e: /* SRL (HL) */
		{
			u8 c = ReadMem(rhl);
			OP_SRL(c)
			WriteMem(rhl, c);
			ADD_CLK(7)
			break;
		}

		case 0x3f: /* SRL A */
			OP_SRL(ra)
			break;

		case 0x40: /* BIT 0,B */
		case 0x48: /* BIT 1,B */
		case 0x50: /* BIT 2,B */
		case 0x58: /* BIT 3,B */
		case 0x60: /* BIT 4,B */
		case 0x68: /* BIT 5,B */
		case 0x70: /* BIT 6,B */
		case 0x78: /* BIT 7,B */
			rf = (rf & ~(FZ | FN)) | FH;
			if ((rb & bitp) == 0) {
				rf |= FZ;
			}
			break;

		case 0x41: /* BIT 0,C */
		case 0x49: /* BIT 1,C */
		case 0x51: /* BIT 2,C */
		case 0x59: /* BIT 3,C */
		case 0x61: /* BIT 4,C */
		case 0x69: /* BIT 5,C */
		case 0x71: /* BIT 6,C */
		case 0x79: /* BIT 7,C */
			rf = (rf & ~(FZ | FN)) | FH;
			if ((rc & bitp) == 0) {
				rf |= FZ;
			}
			break;

		case 0x42: /* BIT 0,D */
		case 0x4a: /* BIT 1,D */
		case 0x52: /* BIT 2,D */
		case 0x5a: /* BIT 3,D */
		case 0x62: /* BIT 4,D */
		case 0x6a: /* BIT 5,D */
		case 0x72: /* BIT 6,D */
		case 0x7a: /* BIT 7,D */
			rf = (rf & ~(FZ | FN)) | FH;
			if ((rd & bitp) == 0) {
				rf |= FZ;
			}
			break;

		case 0x43: /* BIT 0,E */
		case 0x4b: /* BIT 1,E */
		case 0x53: /* BIT 2,E */
		case 0x5b: /* BIT 3,E */
		case 0x63: /* BIT 4,E */
		case 0x6b: /* BIT 5,E */
		case 0x73: /* BIT 6,E */
		case 0x7b: /* BIT 7,E */
			rf = (rf & ~(FZ | FN)) | FH;
			if ((re & bitp) == 0) {
				rf |= FZ;
			}
			break;

		case 0x44: /* BIT 0,H */
		case 0x4c: /* BIT 1,H */
		case 0x54: /* BIT 2,H */
		case 0x5c: /* BIT 3,H */
		case 0x64: /* BIT 4,H */
		case 0x6c: /* BIT 5,H */
		case 0x74: /* BIT 6,H */
		case 0x7c: /* BIT 7,H */
			rf = (rf & ~(FZ | FN)) | FH;
			if ((rh & bitp) == 0) {
				rf |= FZ;
			}
			break;

		case 0x45: /* BIT 0,L */
		case 0x4d: /* BIT 1,L */
		case 0x55: /* BIT 2,L */
		case 0x5d: /* BIT 3,L */
		case 0x65: /* BIT 4,L */
		case 0x6d: /* BIT 5,L */
		case 0x75: /* BIT 6,L */
		case 0x7d: /* BIT 7,L */
			rf = (rf & ~(FZ | FN)) | FH;
			if ((rl & bitp) == 0) {
				rf |= FZ;
			}
			break;

		case 0x46: /* BIT 0,(HL) */
		case 0x4e: /* BIT 1,(HL) */
		case 0x56: /* BIT 2,(HL) */
		case 0x5e: /* BIT 3,(HL) */
		case 0x66: /* BIT 4,(HL) */
		case 0x6e: /* BIT 5,(HL) */
		case 0x76: /* BIT 6,(HL) */
		case 0x7e: /* BIT 7,(HL) */
			rf = (rf & ~(FZ | FN)) | FH;
			if ((ReadMem(rhl) & bitp) == 0) {
				rf |= FZ;
			}
			ADD_CLK(4)
			break;

		case 0x47: /* BIT 0,A */
		case 0x4f: /* BIT 1,A */
		case 0x57: /* BIT 2,A */
		case 0x5f: /* BIT 3,A */
		case 0x67: /* BIT 4,A */
		case 0x6f: /* BIT 5,A */
		case 0x77: /* BIT 6,A */
		case 0x7f: /* BIT 7,A */
			rf = (rf & ~(FZ | FN)) | FH;
			if ((ra & bitp) == 0) {
				rf |= FZ;
			}
			break;

		case 0x80: /* RES 0,B */
		case 0x88: /* RES 1,B */
		case 0x90: /* RES 2,B */
		case 0x98: /* RES 3,B */
		case 0xa0: /* RES 4,B */
		case 0xa8: /* RES 5,B */
		case 0xb0: /* RES 6,B */
		case 0xb8: /* RES 7,B */
			rb &= ~bitp;
			break;

		case 0x81: /* RES 0,C */
		case 0x89: /* RES 1,C */
		case 0x91: /* RES 2,C */
		case 0x99: /* RES 3,C */
		case 0xa1: /* RES 4,C */
		case 0xa9: /* RES 5,C */
		case 0xb1: /* RES 6,C */
		case 0xb9: /* RES 7,C */
			rc &= ~bitp;
			break;

		case 0x82: /* RES 0,D */
		case 0x8a: /* RES 1,D */
		case 0x92: /* RES 2,D */
		case 0x9a: /* RES 3,D */
		case 0xa2: /* RES 4,D */
		case 0xaa: /* RES 5,D */
		case 0xb2: /* RES 6,D */
		case 0xba: /* RES 7,D */
			rd &= ~bitp;
			break;

		case 0x83: /* RES 0,E */
		case 0x8b: /* RES 1,E */
		case 0x93: /* RES 2,E */
		case 0x9b: /* RES 3,E */
		case 0xa3: /* RES 4,E */
		case 0xab: /* RES 5,E */
		case 0xb3: /* RES 6,E */
		case 0xbb: /* RES 7,E */
			re &= ~bitp;
			break;

		case 0x84: /* RES 0,H */
		case 0x8c: /* RES 1,H */
		case 0x94: /* RES 2,H */
		case 0x9c: /* RES 3,H */
		case 0xa4: /* RES 4,H */
		case 0xac: /* RES 5,H */
		case 0xb4: /* RES 6,H */
		case 0xbc: /* RES 7,H */
			rh &= ~bitp;
			break;

		case 0x85: /* RES 0,L */
		case 0x8d: /* RES 1,L */
		case 0x95: /* RES 2,L */
		case 0x9d: /* RES 3,L */
		case 0xa5: /* RES 4,L */
		case 0xad: /* RES 5,L */
		case 0xb5: /* RES 6,L */
		case 0xbd: /* RES 7,L */
			rl &= ~bitp;
			break;

		case 0x86: /* RES 0,(HL) */
		case 0x8e: /* RES 1,(HL) */
		case 0x96: /* RES 2,(HL) */
		case 0x9e: /* RES 3,(HL) */
		case 0xa6: /* RES 4,(HL) */
		case 0xae: /* RES 5,(HL) */
		case 0xb6: /* RES 6,(HL) */
		case 0xbe: /* RES 7,(HL) */
		{
			u8 c = ReadMem(rhl);
			WriteMem(rhl, c & ~bitp);
			ADD_CLK(7)
			break;
		}

		case 0x87: /* RES 0,A */
		case 0x8f: /* RES 1,A */
		case 0x97: /* RES 2,A */
		case 0x9f: /* RES 3,A */
		case 0xa7: /* RES 4,A */
		case 0xaf: /* RES 5,A */
		case 0xb7: /* RES 6,A */
		case 0xbf: /* RES 7,A */
			ra &= ~bitp;
			break;

		case 0xc0: /* SET 0,B */
		case 0xc8: /* SET 1,B */
		case 0xd0: /* SET 2,B */
		case 0xd8: /* SET 3,B */
		case 0xe0: /* SET 4,B */
		case 0xe8: /* SET 5,B */
		case 0xf0: /* SET 6,B */
		case 0xf8: /* SET 7,B */
			rb |= bitp;
			break;

		case 0xc1: /* SET 0,C */
		case 0xc9: /* SET 1,C */
		case 0xd1: /* SET 2,C */
		case 0xd9: /* SET 3,C */
		case 0xe1: /* SET 4,C */
		case 0xe9: /* SET 5,C */
		case 0xf1: /* SET 6,C */
		case 0xf9: /* SET 7,C */
			rc |= bitp;
			break;

		case 0xc2: /* SET 0,D */
		case 0xca: /* SET 1,D */
		case 0xd2: /* SET 2,D */
		case 0xda: /* SET 3,D */
		case 0xe2: /* SET 4,D */
		case 0xea: /* SET 5,D */
		case 0xf2: /* SET 6,D */
		case 0xfa: /* SET 7,D */
			rd |= bitp;
			break;

		case 0xc3: /* SET 0,E */
		case 0xcb: /* SET 1,E */
		case 0xd3: /* SET 2,E */
		case 0xdb: /* SET 3,E */
		case 0xe3: /* SET 4,E */
		case 0xeb: /* SET 5,E */
		case 0xf3: /* SET 6,E */
		case 0xfb: /* SET 7,E */
			re |= bitp;
			break;

		case 0xc4: /* SET 0,H */
		case 0xcc: /* SET 1,H */
		case 0xd4: /* SET 2,H */
		case 0xdc: /* SET 3,H */
		case 0xe4: /* SET 4,H */
		case 0xec: /* SET 5,H */
		case 0xf4: /* SET 6,H */
		case 0xfc: /* SET 7,H */
			rh |= bitp;
			break;

		case 0xc5: /* SET 0,L */
		case 0xcd: /* SET 1,L */
		case 0xd5: /* SET 2,L */
		case 0xdd: /* SET 3,L */
		case 0xe5: /* SET 4,L */
		case 0xed: /* SET 5,L */
		case 0xf5: /* SET 6,L */
		case 0xfd: /* SET 7,L */
			rl |= bitp;
			break;

		case 0xc6: /* SET 0,(HL) */
		case 0xce: /* SET 1,(HL) */
		case 0xd6: /* SET 2,(HL) */
		case 0xde: /* SET 3,(HL) */
		case 0xe6: /* SET 4,(HL) */
		case 0xee: /* SET 5,(HL) */
		case 0xf6: /* SET 6,(HL) */
		case 0xfe: /* SET 7,(HL) */
		{
			u8 c = ReadMem(rhl);
			WriteMem(rhl, c | bitp);
			ADD_CLK(7)
			break;
		}

		case 0xc7: /* SET 0,A */
		case 0xcf: /* SET 1,A */
		case 0xd7: /* SET 2,A */
		case 0xdf: /* SET 3,A */
		case 0xe7: /* SET 4,A */
		case 0xef: /* SET 5,A */
		case 0xf7: /* SET 6,A */
		case 0xff: /* SET 7,A */
			ra |= bitp;
			break;

		default:
			rpc -= 2;
			PrintReg(pr);
			ErrExit("Undefined instruction.");
	}
	ADD_CLK(8)
	return;
}

/* CALL Z,nn */
static void i_cc(z80reg_t *pr)
{
	u16 m = ReadMemI16(pr);
	if ((rf & FZ) != 0) {
		rsp -= 2;
		WriteMem16(rsp, rpc);
		rpc = m;
		ADD_CLK(17)
	} else {
		ADD_CLK(10)
	}
	return;
}

/* CALL nn */
static void i_cd(z80reg_t *pr)
{
	u16 m = ReadMemI16(pr);
	rsp -= 2;
	WriteMem16(rsp, rpc);
	rpc = m;
	ADD_CLK(17)
	return;
}

/* ADC A,n */
static void i_ce(z80reg_t *pr)
{
	u8 c = ReadMemI(pr);
	OP_ADC(ra, c)
	ADD_CLK(7)
	return;
}

/* RST 08H */
static void i_cf(z80reg_t *pr)
{
	rsp -= 2;
	WriteMem16(rsp, rpc);
	rpc = 0x0008;
	ADD_CLK(11)
	return;
}

/* RET NC */
static void i_d0(z80reg_t *pr)
{
	if ((rf & FC) == 0) {
		rpc = ReadMem16(rsp);
		rsp += 2;
		ADD_CLK(11)
	} else {
		ADD_CLK(5)
	}
	return;
}

/* POP DE */
static void i_d1(z80reg_t *pr)
{
	rde = ReadMem16(rsp);
	rsp += 2;
	ADD_CLK(10)
	return;
}

/* JP NC,nn */
static void i_d2(z80reg_t *pr)
{
	u16 m = ReadMemI16(pr);
	if ((rf & FC) == 0) {
		rpc = m;
	}
	ADD_CLK(10)
	return;
}

/* OUT (n),A */
static void i_d3(z80reg_t *pr)
{
	u8 c = ReadMemI(pr);
	WriteIO((ra << 8) + c, ra);
	ADD_CLK(11)
	return;
}

/* CALL NC,nn */
static void i_d4(z80reg_t *pr)
{
	u16 m = ReadMemI16(pr);
	if ((rf & FC) == 0) {
		rsp -= 2;
		WriteMem16(rsp, rpc);
		rpc = m;
		ADD_CLK(17)
	} else {
		ADD_CLK(10)
	}
	return;
}

/* PUSH DE */
static void i_d5(z80reg_t *pr)
{
	rsp -= 2;
	WriteMem16(rsp, rde);
	ADD_CLK(11)
	return;
}

/* SUB n */
static void i_d6(z80reg_t *pr)
{
	u8 c = ReadMemI(pr);
	OP_SUB(ra, c)
	ADD_CLK(7)
	return;
}

/* RST 10H */
static void i_d7(z80reg_t *pr)
{
	rsp -= 2;
	WriteMem16(rsp, rpc);
	rpc = 0x0010;
	ADD_CLK(11)
	return;
}

/* RET C */
static void i_d8(z80reg_t *pr)
{
	if ((rf & FC) != 0) {
		rpc = ReadMem16(rsp);
		rsp += 2;
		ADD_CLK(11)
	} else {
		ADD_CLK(5)
	}
	return;
}

/* EXX */
static void i_d9(z80reg_t *pr)
{
	u16 s = rbc;
	rbc = pr->BCD.HL;
	pr->BCD.HL = s;
	s = rde;
	rde = pr->DED.HL;
	pr->DED.HL = s;
	s = rhl;
	rhl = pr->HLD.HL;
	pr->HLD.HL = s;
	ADD_CLK(4)
	return;
}

/* JP C,nn */
static void i_da(z80reg_t *pr)
{
	u16 m = ReadMemI16(pr);
	if ((rf & FC) != 0) {
		rpc = m;
	}
	ADD_CLK(10)
	return;
}

/* IN A,(n) */
static void i_db(z80reg_t *pr)
{
	u8 c = ReadMemI(pr);
	ra = ReadIO((ra << 8) + c);
	ADD_CLK(11)
	return;
}

/* CALL C,nn */
static void i_dc(z80reg_t *pr)
{
	u16 m = ReadMemI16(pr);
	if ((rf & FC) != 0) {
		rsp -= 2;
		WriteMem16(rsp, rpc);
		rpc = m;
		ADD_CLK(17)
	} else {
		ADD_CLK(10)
	}
	return;
}

/* DD xx */
/* FD xx */
static void i_dd_fd(z80reg_t *pr, u16 *pxy) {
	pr->R++;
	switch(ReadMemI(pr)) {
		case 0x09: /* ADD IX,BC */
		{
			u32 l = (u32)*pxy + (u32)rbc;
			*pxy = (u16)l;
			rf &= ~(FN | FC);
			if ((l & 0x10000) != 0) {
				rf |= FC;
			}
			ADD_CLK(15)
			break;
		}

		case 0x19: /* ADD IX,DE */
		{
			u32 l = (u32)*pxy + (u32)rde;
			*pxy = (u16)l;
			rf &= ~(FN | FC);
			if ((l & 0x10000) != 0) {
				rf |= FC;
			}
			ADD_CLK(15)
			break;
		}

		case 0x21: /* LD IX,nn */
			*pxy = ReadMemI16(pr);
			ADD_CLK(14)
			break;

		case 0x22: /* LD (nn),IX */
		{
			u16 m = ReadMemI16(pr);
			WriteMem16(m, *pxy);
			ADD_CLK(20)
			break;
		}

		case 0x23: /* INC IX */
			(*pxy)++;
			ADD_CLK(10)
			break;

		case 0x29: /* ADD IX,IX */
			rf &= ~(FN | FC);
			if ((*pxy & 0x8000) != 0) {
				rf |= FC;
			}
			*pxy += *pxy;
			ADD_CLK(15)
			break;

		case 0x2a: /* LD IX,(nn) */
		{
			u16 m = ReadMemI16(pr);
			*pxy = ReadMem16(m);
			ADD_CLK(20)
			break;
		}

		case 0x2b: /* DEC IX */
			(*pxy)--;
			ADD_CLK(10)
			break;

		case 0x34: /* INC (IX+d) */
		{
			s8 ofst = ReadMemI(pr);
			u8 c = ReadMem(*pxy + ofst);
			OP_INC(c)
			WriteMem(*pxy + ofst, c);
			ADD_CLK(23)
			break;
		}

		case 0x35: /* DEC (IX+d) */
		{
			s8 ofst = ReadMemI(pr);
			u8 c = ReadMem(*pxy + ofst);
			OP_DEC(c)
			WriteMem(*pxy + ofst, c);
			ADD_CLK(23)
			break;
		}

		case 0x36: /* LD (IX+d),n */
		{
			s8 ofst = ReadMemI(pr);
			WriteMem(*pxy + ofst, ReadMemI(pr));
			ADD_CLK(19)
			break;
		}

		case 0x39: /* ADD IX,SP */
		{
			u32 l = (u32)*pxy + (u32)rsp;
			*pxy = (u16)l;
			rf &= ~(FN | FC);
			if ((l & 0x10000) != 0) {
				rf |= FC;
			}
			ADD_CLK(11)
			break;
		}

		case 0x46: /* LD B,(IX+d) */
		{
			s8 ofst = ReadMemI(pr);
			rb = ReadMem(*pxy + ofst);
			ADD_CLK(19)
			break;
		}

		case 0x4e: /* LD C,(IX+d) */
		{
			s8 ofst = ReadMemI(pr);
			rc = ReadMem(*pxy + ofst);
			ADD_CLK(19)
			break;
		}

		case 0x56: /* LD D,(IX+d) */
		{
			s8 ofst = ReadMemI(pr);
			rd = ReadMem(*pxy + ofst);
			ADD_CLK(19)
			break;
		}

		case 0x5e: /* LD E,(IX+d) */
		{
			s8 ofst = ReadMemI(pr);
			re = ReadMem(*pxy + ofst);
			ADD_CLK(19)
			break;
		}

		case 0x66: /* LD H,(IX+d) */
		{
			s8 ofst = ReadMemI(pr);
			rh = ReadMem(*pxy + ofst);
			ADD_CLK(19)
			break;
		}

		case 0x6e: /* LD L,(IX+d) */
		{
			s8 ofst = ReadMemI(pr);
			rl = ReadMem(*pxy + ofst);
			ADD_CLK(19)
			break;
		}

		case 0x70: /* LD (IX+d),B */
		{
			s8 ofst = ReadMemI(pr);
			WriteMem(*pxy + ofst, rb);
			ADD_CLK(19)
			break;
		}

		case 0x71: /* LD (IX+d),C */
		{
			s8 ofst = ReadMemI(pr);
			WriteMem(*pxy + ofst, rc);
			ADD_CLK(19)
			break;
		}

		case 0x72: /* LD (IX+d),D */
		{
			s8 ofst = ReadMemI(pr);
			WriteMem(*pxy + ofst, rd);
			ADD_CLK(19)
			break;
		}

		case 0x73: /* LD (IX+d),E */
		{
			s8 ofst = ReadMemI(pr);
			WriteMem(*pxy + ofst, re);
			ADD_CLK(19)
			break;
		}

		case 0x74: /* LD (IX+d),H */
		{
			s8 ofst = ReadMemI(pr);
			WriteMem(*pxy + ofst, rh);
			ADD_CLK(19)
			break;
		}

		case 0x75: /* LD (IX+d),L */
		{
			s8 ofst = ReadMemI(pr);
			WriteMem(*pxy + ofst, rl);
			ADD_CLK(19)
			break;
		}

		case 0x77: /* LD (IX+d),A */
		{
			s8 ofst = ReadMemI(pr);
			WriteMem(*pxy + ofst, ra);
			ADD_CLK(19)
			break;
		}

		case 0x7e: /* LD A,(IX+d) */
		{
			s8 ofst = ReadMemI(pr);
			ra = ReadMem(*pxy + ofst);
			ADD_CLK(19)
			break;
		}

		case 0x86: /* ADD A,(IX+d) */
		{
			s8 ofst = ReadMemI(pr);
			u8 c = ReadMem(*pxy + ofst);
			OP_ADD(ra, c)
			ADD_CLK(19)
			break;
		}

		case 0x8e: /* ADC A,(IX+d) */
		{
			s8 ofst = ReadMemI(pr);
			u8 c = ReadMem(*pxy + ofst);
			OP_ADC(ra, c)
			ADD_CLK(19)
			break;
		}

		case 0x96: /* SUB (IX+d) */
		{
			s8 ofst = ReadMemI(pr);
			u8 c = ReadMem(*pxy + ofst);
			OP_SUB(ra, c)
			ADD_CLK(19)
			break;
		}

		case 0x9e: /* SBC A,(IX+d) */
		{
			s8 ofst = ReadMemI(pr);
			u8 c = ReadMem(*pxy + ofst);
			OP_SBC(ra, c)
			ADD_CLK(19)
			break;
		}

		case 0xa6: /* AND (IX+d) */
		{
			s8 ofst = ReadMemI(pr);
			ra &= ReadMem(*pxy + ofst);
			rf = s_szpTbl[ra] | FH;
			ADD_CLK(19)
			break;
		}

		case 0xae: /* XOR (IX+d) */
		{
			s8 ofst = ReadMemI(pr);
			ra ^= ReadMem(*pxy + ofst);
			rf = s_szpTbl[ra];
			ADD_CLK(19)
			break;
		}

		case 0xb6: /* OR (IX+d) */
		{
			s8 ofst = ReadMemI(pr);
			ra |= ReadMem(*pxy + ofst);
			rf = s_szpTbl[ra];
			ADD_CLK(19)
			break;
		}

		case 0xbe: /* CP (IX+d) */
		{
			s8 ofst = ReadMemI(pr);
			u8 c = ReadMem(*pxy + ofst);
			OP_CP(ra, c)
			ADD_CLK(19)
			break;
		}

		case 0xcb:
		{
			s8 ofst = ReadMemI(pr);
			u8 c = ReadMem(*pxy + ofst);
			u8 cbn = ReadMemI(pr);
			u8 bitp = 1 << ((cbn >> 3) & 0x07);
			switch(cbn) {
				case 0x06: /* RLC (IX+d) */
					OP_RLC(c)
					WriteMem(*pxy + ofst, c);
					ADD_CLK(23)
					break;

				case 0x0e: /* RRC (IX+d) */
					OP_RRC(c)
					WriteMem(*pxy + ofst, c);
					ADD_CLK(23)
					break;

				case 0x16: /* RL (IX+d) */
					OP_RL(c)
					WriteMem(*pxy + ofst, c);
					ADD_CLK(23)
					break;

				case 0x1e: /* RR (IX+d) */
					OP_RR(c)
					WriteMem(*pxy + ofst, c);
					ADD_CLK(23)
					break;

				case 0x26: /* SLA (IX+d) */
					OP_SLA(c)
					WriteMem(*pxy + ofst, c);
					ADD_CLK(23)
					break;

				case 0x2e: /* SRA (IX+d) */
					OP_SRA(c)
					WriteMem(*pxy + ofst, c);
					ADD_CLK(23)
					break;

				case 0x3e: /* SRL (IX+d) */
					OP_SRL(c)
					WriteMem(*pxy + ofst, c);
					ADD_CLK(23)
					break;

				case 0x46: /* BIT 0,(IX+d) */
				case 0x4e: /* BIT 1,(IX+d) */
				case 0x56: /* BIT 2,(IX+d) */
				case 0x5e: /* BIT 3,(IX+d) */
				case 0x66: /* BIT 4,(IX+d) */
				case 0x6e: /* BIT 5,(IX+d) */
				case 0x76: /* BIT 6,(IX+d) */
				case 0x7e: /* BIT 7,(IX+d) */
					rf = (rf & ~(FZ | FN)) | FH;
					if ((c & bitp) == 0) {
						rf |= FZ;
					}
					ADD_CLK(20)
					break;

				case 0x86: /* RES 0,(IX+d) */
				case 0x8e: /* RES 1,(IX+d) */
				case 0x96: /* RES 2,(IX+d) */
				case 0x9e: /* RES 3,(IX+d) */
				case 0xa6: /* RES 4,(IX+d) */
				case 0xae: /* RES 5,(IX+d) */
				case 0xb6: /* RES 6,(IX+d) */
				case 0xbe: /* RES 7,(IX+d) */
					WriteMem(*pxy + ofst, c & ~bitp);
					ADD_CLK(23)
					break;

				case 0xc6: /* SET 0,(IX+d) */
				case 0xce: /* SET 1,(IX+d) */
				case 0xd6: /* SET 2,(IX+d) */
				case 0xde: /* SET 3,(IX+d) */
				case 0xe6: /* SET 4,(IX+d) */
				case 0xee: /* SET 5,(IX+d) */
				case 0xf6: /* SET 6,(IX+d) */
				case 0xfe: /* SET 7,(IX+d) */
					WriteMem(*pxy + ofst, c | bitp);
					ADD_CLK(23)
					break;

				default:
					rpc -= 4;
					PrintReg(pr);
					ErrExit("Undefined instruction.");
			}
			break;
		}

		case 0xe1: /* POP IX */
			*pxy = ReadMem16(rsp);
			rsp += 2;
			ADD_CLK(14)
			break;

		case 0xe3: /* EX (SP),IX */
		{
			u16 m = ReadMem16(rsp);
			WriteMem16(rsp, *pxy);
			*pxy = m;
			ADD_CLK(23)
			break;
		}

		case 0xe5: /* PUSH IX */
			rsp -= 2;
			WriteMem16(rsp, *pxy);
			ADD_CLK(15)
			break;

		case 0xe9: /* JP (IX) */
			rpc = *pxy;
			ADD_CLK(8)
			break;

		case 0xf9: /* LD SP,IX */
			rsp = *pxy;
			ADD_CLK(10)
			break;

		default:
			rpc -= 2;
			PrintReg(pr);
			ErrExit("Undefined instruction.");
	}
	return;
}

/* DD xx */
static void i_dd(z80reg_t *pr)
{
	i_dd_fd(pr, &pr->IX.HL);
	return;
}

/* SBC A,n */
static void i_de(z80reg_t *pr)
{
	u8 c = ReadMemI(pr);
	OP_SBC(ra, c)
	ADD_CLK(7)
	return;
}

/* RST 18H */
static void i_df(z80reg_t *pr)
{
	rsp -= 2;
	WriteMem16(rsp, rpc);
	rpc = 0x0018;
	ADD_CLK(11)
	return;
}

/* RET PO */
static void i_e0(z80reg_t *pr)
{
	if ((rf & FPV) == 0) {
		rpc = ReadMem16(rsp);
		rsp += 2;
		ADD_CLK(11)
	} else {
		ADD_CLK(5)
	}
	return;
}

/* POP HL */
static void i_e1(z80reg_t *pr)
{
	rhl = ReadMem16(rsp);
	rsp += 2;
	ADD_CLK(10)
	return;
}

/* JP PO,nn */
static void i_e2(z80reg_t *pr)
{
	u16 m = ReadMemI16(pr);
	if ((rf & FPV) == 0) {
		rpc = m;
	}
	ADD_CLK(10)
	return;
}

/* EX (SP),HL */
static void i_e3(z80reg_t *pr)
{
	u16 m = ReadMem16(rsp);
	WriteMem16(rsp, rhl);
	rhl = m;
	ADD_CLK(19)
	return;
}

/* CALL PO,nn */
static void i_e4(z80reg_t *pr)
{
	u16 m = ReadMemI16(pr);
	if ((rf & FPV) == 0) {
		rsp -= 2;
		WriteMem16(rsp, rpc);
		rpc = m;
		ADD_CLK(17)
	} else {
		ADD_CLK(10)
	}
	return;
}

/* PUSH HL */
static void i_e5(z80reg_t *pr)
{
	rsp -= 2;
	WriteMem16(rsp, rhl);
	ADD_CLK(11)
	return;
}

/* AND n */
static void i_e6(z80reg_t *pr)
{
	ra &= ReadMemI(pr);
	rf = s_szpTbl[ra] | FH;
	ADD_CLK(7)
	return;
}

/* RST 20H */
static void i_e7(z80reg_t *pr)
{
	rsp -= 2;
	WriteMem16(rsp, rpc);
	rpc = 0x0020;
	ADD_CLK(11)
	return;
}

/* RET PE */
static void i_e8(z80reg_t *pr)
{
	if ((rf & FPV) != 0) {
		rpc = ReadMem16(rsp);
		rsp += 2;
		ADD_CLK(11)
	} else {
		ADD_CLK(5)
	}
	return;
}

/* JP (HL) */
static void i_e9(z80reg_t *pr)
{
	rpc = rhl;
	ADD_CLK(4)
	return;
}

/* JP PE,nn */
static void i_ea(z80reg_t *pr)
{
	u16 m = ReadMemI16(pr);
	if ((rf & FPV) != 0) {
		rpc = m;
	}
	ADD_CLK(10)
	return;
}

/* EX DE,HL */
static void i_eb(z80reg_t *pr)
{
	u16 s = rde;
	rde = rhl;
	rhl = s;
	ADD_CLK(4)
	return;
}

/* CALL PE,nn */
static void i_ec(z80reg_t *pr)
{
	u16 m = ReadMemI16(pr);
	if ((rf & FPV) != 0) {
		rsp -= 2;
		WriteMem16(rsp, rpc);
		rpc = m;
		ADD_CLK(17)
	} else {
		ADD_CLK(10)
	}
	return;
}

/* ED xx */
static void i_ed(z80reg_t *pr)
{
	pr->R++;
	switch(ReadMemI(pr)) {
		case 0x40: /* IN B,(C) */
			rb = ReadIO(rbc);
			rf = s_szpTbl[rb] | (rf & FC);
			ADD_CLK(12)
			break;

		case 0x41: /* OUT (C),B */
			WriteIO(rbc, rb);
			ADD_CLK(12)
			break;

		case 0x42: /* SBC HL,BC */
			OP_SBC(rl, rc)
			OP_SBC(rh, rb)
			if (rhl == 0) {
				rf |= FZ;
			} else {
				rf &= ~FZ;
			}
			ADD_CLK(15)
			break;

		case 0x43: /* LD (nn),BC */
		{
			u16 m = ReadMemI16(pr);
			WriteMem16(m, rbc);
			ADD_CLK(20)
			break;
		}

		case 0x44: /* NEG */
			rf = s_subTbl[ra << 8];
			ra = -ra;
			ADD_CLK(8)
			break;

		case 0x45: /* RETN */
			rpc = ReadMem16(rsp);
			rsp += 2;
			ADD_CLK(14)
			break;

		case 0x46: /* IM 0 */
			pr->IM = 0;
			ADD_CLK(8)
			break;

		case 0x47: /* LD I,A */
			pr->I = ra;
			ADD_CLK(9)
			break;

		case 0x48: /* IN C,(C) */
			rc = ReadIO(rbc);
			rf = s_szpTbl[rc] | (rf & FC);
			ADD_CLK(12)
			break;

		case 0x49: /* OUT (C),C */
			WriteIO(rbc, rc);
			ADD_CLK(12)
			break;

		case 0x4a: /* ADC HL,BC */
			OP_ADC(rl, rc)
			OP_ADC(rh, rb)
			if (rhl == 0) {
				rf |= FZ;
			} else {
				rf &= ~FZ;
			}
			ADD_CLK(15)
			break;

		case 0x4b: /* LD BC,(nn) */
		{
			u16 m = ReadMemI16(pr);
			rbc = ReadMem16(m);
			ADD_CLK(20)
			break;
		}

		case 0x4d: /* RETI */
			rpc = ReadMem16(rsp);
			rsp += 2;
			ADD_CLK(14)
			break;

		case 0x4f: /* LD R,A */
			pr->R = ra;
			pr->R_MSB = ra;
			ADD_CLK(9)
			break;

		case 0x50: /* IN D,(C) */
			rd = ReadIO(rbc);
			rf = s_szpTbl[rd] | (rf & FC);
			ADD_CLK(12)
			break;

		case 0x51: /* OUT (C),D */
			WriteIO(rbc, rd);
			ADD_CLK(12)
			break;

		case 0x52: /* SBC HL,DE */
			OP_SBC(rl, re)
			OP_SBC(rh, rd)
			if (rhl == 0) {
				rf |= FZ;
			} else {
				rf &= ~FZ;
			}
			ADD_CLK(15)
			break;

		case 0x53: /* LD (nn),DE */
		{
			u16 m = ReadMemI16(pr);
			WriteMem16(m, rde);
			ADD_CLK(20)
			break;
		}

		case 0x56: /* IM 1 */
			pr->IM = 1;
			ADD_CLK(8)
			break;

		case 0x57: /* LD A,I */
			ra = pr->I;
			rf = (rf & FC) | (s_szpTbl[ra] & ~FPV) | (pr->IFF << 2);
			ADD_CLK(9)
			break;

		case 0x58: /* IN E,(C) */
			re = ReadIO(rbc);
			rf = s_szpTbl[re] | (rf & FC);
			ADD_CLK(12)
			break;

		case 0x59: /* OUT (C),E */
			WriteIO(rbc, re);
			ADD_CLK(12)
			break;

		case 0x5a: /* ADC HL,DE */
			OP_ADC(rl, re)
			OP_ADC(rh, rd)
			if (rhl == 0) {
				rf |= FZ;
			} else {
				rf &= ~FZ;
			}
			ADD_CLK(15)
			break;

		case 0x5b: /* LD DE,(nn) */
		{
			u16 m = ReadMemI16(pr);
			rde = ReadMem16(m);
			ADD_CLK(20)
			break;
		}

		case 0x5e: /* IM 2 */
			pr->IM = 2;
			ADD_CLK(8)
			break;

		case 0x5f: /* LD A,R */
			ra = (pr->R_MSB & 0x80) | (pr->R & 0x7f);
			rf = (rf & FC) | (s_szpTbl[ra] & ~FPV) | (pr->IFF << 2);
			ADD_CLK(9)
			break;

		case 0x60: /* IN H,(C) */
			rh = ReadIO(rbc);
			rf = s_szpTbl[rh] | (rf & FC);
			ADD_CLK(12)
			break;

		case 0x61: /* OUT (C),H */
			WriteIO(rbc, rh);
			ADD_CLK(12)
			break;

		case 0x62: /* SBC HL,HL */
			OP_SBC(rl, rl)
			OP_SBC(rh, rh)
			if (rhl == 0) {
				rf |= FZ;
			} else {
				rf &= ~FZ;
			}
			ADD_CLK(15)
			break;

		case 0x67: /* RRD */
		{
			u8 c = ReadMem(rhl);
			WriteMem(rhl, (c >> 4) | (ra << 4));
			ra = (ra & 0xf0) | (c & 0x0f);
			rf = (rf & FC) | s_szpTbl[ra];
			ADD_CLK(18)
			break;
		}

		case 0x68: /* IN L,(C) */
			rl = ReadIO(rbc);
			rf = s_szpTbl[rl] | (rf & FC);
			ADD_CLK(12)
			break;

		case 0x69: /* OUT (C),L */
			WriteIO(rbc, rl);
			ADD_CLK(12)
			break;

		case 0x6a: /* ADC HL,HL */
			OP_ADC(rl, rl)
			OP_ADC(rh, rh)
			if (rhl == 0) {
				rf |= FZ;
			} else {
				rf &= ~FZ;
			}
			ADD_CLK(15)
			break;

		case 0x6f: /* RLD */
		{
			u8 c = ReadMem(rhl);
			WriteMem(rhl, (c << 4) | (ra & 0x0f));
			ra = (ra & 0xf0) | (c >> 4);
			rf = (rf & FC) | s_szpTbl[ra];
			ADD_CLK(18)
			break;
		}

		case 0x70: /* IN (C) */
		{
			u8 c = ReadIO(rbc);
			rf = s_szpTbl[c] | (rf & FC);
			ADD_CLK(12)
			break;
		}

		case 0x72: /* SBC HL,SP */
		{
			r16 r;
			r.HL = rsp;
			OP_SBC(rl, r.B.L)
			OP_SBC(rh, r.B.H)
			if (rhl == 0) {
				rf |= FZ;
			} else {
				rf &= ~FZ;
			}
			ADD_CLK(15)
			break;
		}

		case 0x73: /* LD (nn),SP */
		{
			u16 m = ReadMemI16(pr);
			WriteMem16(m, rsp);
			ADD_CLK(20)
			break;
		}

		case 0x78: /* IN A,(C) */
			ra = ReadIO(rbc);
			rf = s_szpTbl[ra] | (rf & FC);
			ADD_CLK(12)
			break;

		case 0x79: /* OUT (C),A */
			WriteIO(rbc, ra);
			ADD_CLK(12)
			break;

		case 0x7a: /* ADC HL,SP */
		{
			r16 r;
			r.HL = rsp;
			OP_ADC(rl, r.B.L)
			OP_ADC(rh, r.B.H)
			if (rhl == 0) {
				rf |= FZ;
			} else {
				rf &= ~FZ;
			}
			ADD_CLK(15)
			break;
		}

		case 0x7b: /* LD SP,(nn) */
		{
			u16 m = ReadMemI16(pr);
			rsp = ReadMem16(m);
			ADD_CLK(20)
			break;
		}

		case 0xa0: /* LDI */
		{
			u8 c = ReadMem(rhl++);
			WriteMem(rde++, c);
			rbc--;
			rf &= ~(FH | FPV | FN);
			if (rbc != 0) {
				rf |= FPV;
			}
			ADD_CLK(16)
			break;
		}

		case 0xa1: /* CPI */
		{
			u8 c = ReadMem(rhl++);
			rbc--;
			rf = (rf & FC) | (s_subTbl[(c << 8) + ra] & ~(FPV | FC));
			if (rbc != 0) {
				rf |= FPV;
			}
			ADD_CLK(16)
			break;
		}

		case 0xa2: /* INI */
		{
			u8 c = ReadIO(rbc);
			WriteMem(rhl++, c);
			rb--;
			rf |= FZ | FN;
			if (rb != 0) {
				rf &= ~FZ;
			}
			ADD_CLK(16)
			break;
		}

		case 0xa3: /* OUTI */
		{
			u8 c = ReadMem(rhl++);
			WriteIO(rbc, c);
			rb--;
			rf |= FZ | FN;
			if (rb != 0) {
				rf &= ~FZ;
			}
			ADD_CLK(16)
			break;
		}

		case 0xa8: /* LDD */
		{
			u8 c = ReadMem(rhl--);
			WriteMem(rde--, c);
			rbc--;
			rf &= ~(FH | FPV | FN);
			if (rbc != 0) {
				rf |= FPV;
			}
			ADD_CLK(16)
			break;
		}

		case 0xa9: /* CPD */
		{
			u8 c = ReadMem(rhl--);
			rbc--;
			rf = (rf & FC) | (s_subTbl[(c << 8) + ra] & ~(FPV | FC));
			if (rbc != 0) {
				rf |= FPV;
			}
			ADD_CLK(16)
			break;
		}

		case 0xaa: /* IND */
		{
			u8 c = ReadIO(rbc);
			WriteMem(rhl--, c);
			rb--;
			rf |= FZ | FN;
			if (rb != 0) {
				rf &= ~FZ;
			}
			ADD_CLK(16)
			break;
		}

		case 0xab: /* OUTD */
		{
			u8 c = ReadMem(rhl--);
			WriteIO(rbc, c);
			rb--;
			rf |= FZ | FN;
			if (rb != 0) {
				rf &= ~FZ;
			}
			break;
		}

		case 0xb0: /* LDIR */
		{
			u8 c = ReadMem(rhl++);
			WriteMem(rde++, c);
			rbc--;
			rf &= ~(FH | FPV | FN);
			if (rbc != 0) {
				rf |= FPV;
			}
			if (rbc == 0) {
				ADD_CLK(16)
			} else {
				rpc -= 2;
				ADD_CLK(21)
			}
			break;
		}

		case 0xb1: /* CPIR */
		{
			u8 c = ReadMem(rhl++);
			rbc--;
			rf = (rf & FC) | (s_subTbl[(c << 8) + ra] & ~(FPV | FC));
			if (rbc != 0) {
				rf |= FPV;
			}
			if (ra == c || rbc == 0) {
				ADD_CLK(16)
			} else {
				rpc -= 2;
				ADD_CLK(21)
			}
			break;
		}

		case 0xb2: /* INIR */
		{
			u8 c = ReadIO(rbc);
			WriteMem(rhl++, c);
			rb--;
			rf |= FZ | FN;
			if (rb == 0) {
				ADD_CLK(16)
			} else {
				rf &= ~FZ;
				rpc -= 2;
				ADD_CLK(21)
			}
			break;
		}

		case 0xb3: /* OTIR */
		{
			u8 c = ReadMem(rhl++);
			WriteIO(rbc, c);
			rb--;
			rf |= FZ | FN;
			if (rb == 0) {
				ADD_CLK(16)
			} else {
				rf &= ~FZ;
				rpc -= 2;
				ADD_CLK(21)
			}
			break;
		}

		case 0xb8: /* LDDR */
		{
			u8 c = ReadMem(rhl--);
			WriteMem(rde--, c);
			rbc--;
			rf &= ~(FH | FPV | FN);
			if (rbc != 0) {
				rf |= FPV;
			}
			if (rbc == 0) {
				ADD_CLK(16)
			} else {
				rpc -= 2;
				ADD_CLK(21)
			}
			break;
		}

		case 0xb9: /* CPDR */
		{
			u8 c = ReadMem(rhl--);
			rbc--;
			rf = (rf & FC) | (s_subTbl[(c << 8) + ra] & ~(FPV | FC));
			if (rbc != 0) {
				rf |= FPV;
			}
			if (ra == c || rbc == 0) {
				ADD_CLK(16)
			} else {
				rpc -= 2;
				ADD_CLK(21)
			}
			break;
		}

		case 0xba: /* INDR */
		{
			u8 c = ReadIO(rbc);
			WriteMem(rhl--, c);
			rb--;
			rf |= FZ | FN;
			if (rb == 0) {
				ADD_CLK(16)
			} else {
				rf &= ~FZ;
				rpc -= 2;
				ADD_CLK(21)
			}
			break;
		}

		case 0xbb: /* OTDR */
		{
			u8 c = ReadMem(rhl--);
			WriteIO(rbc, c);
			rb--;
			rf |= FZ | FN;
			if (rb == 0) {
				ADD_CLK(16)
			} else {
				rf &= ~FZ;
				rpc -= 2;
				ADD_CLK(21)
			}
			break;
		}

		default:
			rpc -= 2;
			PrintReg(pr);
			ErrExit("Undefined instruction.");
	}
	return;
}

/* XOR n */
static void i_ee(z80reg_t *pr)
{
	ra ^= ReadMemI(pr);
	rf = s_szpTbl[ra];
	ADD_CLK(7)
	return;
}

/* RST 28H */
static void i_ef(z80reg_t *pr)
{
	rsp -= 2;
	WriteMem16(rsp, rpc);
	rpc = 0x0028;
	ADD_CLK(11)
	return;
}

/* RET P */
static void i_f0(z80reg_t *pr)
{
	if ((rf & FS) == 0) {
		rpc = ReadMem16(rsp);
		rsp += 2;
		ADD_CLK(11)
	} else {
		ADD_CLK(5)
	}
	return;
}

/* POP AF */
static void i_f1(z80reg_t *pr)
{
	raf = ReadMem16(rsp);
	rsp += 2;
	ADD_CLK(10)
	return;
}

/* JP P,nn */
static void i_f2(z80reg_t *pr)
{
	u16 m = ReadMemI16(pr);
	if ((rf & FS) == 0) {
		rpc = m;
	}
	ADD_CLK(10)
	return;
}

/* DI */
static void i_f3(z80reg_t *pr)
{
	pr->IFF = 0;
	ADD_CLK(4)
	return;
}

/* CALL P,nn */
static void i_f4(z80reg_t *pr)
{
	u16 m = ReadMemI16(pr);
	if ((rf & FS) == 0) {
		rsp -= 2;
		WriteMem16(rsp, rpc);
		rpc = m;
		ADD_CLK(17)
	} else {
		ADD_CLK(10)
	}
	return;
}

/* PUSH AF */
static void i_f5(z80reg_t *pr)
{
	rsp -= 2;
	WriteMem16(rsp, raf);
	ADD_CLK(11)
	return;
}

/* OR n */
static void i_f6(z80reg_t *pr)
{
	ra |= ReadMemI(pr);
	rf = s_szpTbl[ra];
	ADD_CLK(7)
	return;
}

/* RST 30H */
static void i_f7(z80reg_t *pr)
{
	rsp -= 2;
	WriteMem16(rsp, rpc);
	rpc = 0x0030;
	ADD_CLK(11)
	return;
}

/* RET M */
static void i_f8(z80reg_t *pr)
{
	if ((rf & FS) != 0) {
		rpc = ReadMem16(rsp);
		rsp += 2;
		ADD_CLK(11)
	} else {
		ADD_CLK(5)
	}
	return;
}

/* LD SP,HL */
static void i_f9(z80reg_t *pr)
{
	rsp = rhl;
	ADD_CLK(6)
	return;
}

/* JP M,nn */
static void i_fa(z80reg_t *pr)
{
	u16 m = ReadMemI16(pr);
	if ((rf & FS) != 0) {
		rpc = m;
	}
	ADD_CLK(10)
	return;
}

/* EI */
static void i_fb(z80reg_t *pr)
{
	pr->IFF = 1; /* -> after next instruction */
	ADD_CLK(4)
	return;
}

/* CALL M,nn */
static void i_fc(z80reg_t *pr)
{
	u16 m = ReadMemI16(pr);
	if ((rf & FS) != 0) {
		rsp -= 2;
		WriteMem16(rsp, rpc);
		rpc = m;
		ADD_CLK(17)
	} else {
		ADD_CLK(10)
	}
	return;
}

/* FD xx */
static void i_fd(z80reg_t *pr)
{
	i_dd_fd(pr, &pr->IY.HL);
	return;
}

/* CP n */
static void i_fe(z80reg_t *pr)
{
	u8 c = ReadMemI(pr);
	OP_CP(ra, c)
	ADD_CLK(7)
	return;
}

/* RST 38H */
static void i_ff(z80reg_t *pr)
{
	rsp -= 2;
	WriteMem16(rsp, rpc);
	rpc = 0x0038;
	ADD_CLK(11)
	return;
}

static void (*instTbl[])(z80reg_t *) = {
	i_00, i_01, i_02, i_03, i_04, i_05, i_06, i_07,
	i_08, i_09, i_0a, i_0b, i_0c, i_0d, i_0e, i_0f,
	i_10, i_11, i_12, i_13, i_14, i_15, i_16, i_17,
	i_18, i_19, i_1a, i_1b, i_1c, i_1d, i_1e, i_1f,
	i_20, i_21, i_22, i_23, i_24, i_25, i_26, i_27,
	i_28, i_29, i_2a, i_2b, i_2c, i_2d, i_2e, i_2f,
	i_30, i_31, i_32, i_33, i_34, i_35, i_36, i_37,
	i_38, i_39, i_3a, i_3b, i_3c, i_3d, i_3e, i_3f,
	i_40, i_41, i_42, i_43, i_44, i_45, i_46, i_47,
	i_48, i_49, i_4a, i_4b, i_4c, i_4d, i_4e, i_4f,
	i_50, i_51, i_52, i_53, i_54, i_55, i_56, i_57,
	i_58, i_59, i_5a, i_5b, i_5c, i_5d, i_5e, i_5f,
	i_60, i_61, i_62, i_63, i_64, i_65, i_66, i_67,
	i_68, i_69, i_6a, i_6b, i_6c, i_6d, i_6e, i_6f,
	i_70, i_71, i_72, i_73, i_74, i_75, i_76, i_77,
	i_78, i_79, i_7a, i_7b, i_7c, i_7d, i_7e, i_7f,
	i_80, i_81, i_82, i_83, i_84, i_85, i_86, i_87,
	i_88, i_89, i_8a, i_8b, i_8c, i_8d, i_8e, i_8f,
	i_90, i_91, i_92, i_93, i_94, i_95, i_96, i_97,
	i_98, i_99, i_9a, i_9b, i_9c, i_9d, i_9e, i_9f,
	i_a0, i_a1, i_a2, i_a3, i_a4, i_a5, i_a6, i_a7,
	i_a8, i_a9, i_aa, i_ab, i_ac, i_ad, i_ae, i_af,
	i_b0, i_b1, i_b2, i_b3, i_b4, i_b5, i_b6, i_b7,
	i_b8, i_b9, i_ba, i_bb, i_bc, i_bd, i_be, i_bf,
	i_c0, i_c1, i_c2, i_c3, i_c4, i_c5, i_c6, i_c7,
	i_c8, i_c9, i_ca, i_cb, i_cc, i_cd, i_ce, i_cf,
	i_d0, i_d1, i_d2, i_d3, i_d4, i_d5, i_d6, i_d7,
	i_d8, i_d9, i_da, i_db, i_dc, i_dd, i_de, i_df,
	i_e0, i_e1, i_e2, i_e3, i_e4, i_e5, i_e6, i_e7,
	i_e8, i_e9, i_ea, i_eb, i_ec, i_ed, i_ee, i_ef,
	i_f0, i_f1, i_f2, i_f3, i_f4, i_f5, i_f6, i_f7,
	i_f8, i_f9, i_fa, i_fb, i_fc, i_fd, i_fe, i_ff,
};

void InitZ80()
{
	int i, j, k;
	r16 hl;
	u8 *p1, *p2;

	hl.B.H = 0x12;
	hl.B.L = 0x34;
	if (hl.HL == 0x3412) {
		ErrExit("Endian is wrong!(check Z80_BIG_ENDIAN macro)");
	} else if (hl.HL != 0x1234) {
		ErrExit("Internal error!(maybe a bug)");
	}

	for(i = 0; i < 0x100; i++) {
		u8 c = i & FS;
		if (i == 0x80) {
			c |= FPV;
		}
		if ((i & 0x0f) == 0) {
			c |= FH;
		}
		if (i == 0) {
			c |= FZ;
		}
		s_incTbl[i] = c;
	}

	for(i = 0; i < 0x100; i++) {
		u8 c = (i & FS) | FN;
		if (i == 0x7f) {
			c |= FPV;
		}
		if ((i & 0x0f) == 0x0f) {
			c |= FH;
		}
		if (i == 0) {
			c |= FZ;
		}
		s_decTbl[i] = c;
	}

	for(i = 0; i < 0x100; i++) {
		u8 c = (i & FS) | FPV;
		int n = i;
		for(j = 0; j < 8; j++) {
			if ((n & 1) != 0) {
				c ^= FPV;
			}
			n >>= 1;
		}
		if (i == 0) {
			c |= FZ;
		}
		s_szpTbl[i] = c;
	}

	p1 = &s_daaATbl[0];
	p2 = &s_daaFTbl[0];
	for(i = 0; i < 8; i++) {
		for(j = 0; j < 0x100; j++) {
			u8 c1 = j;
			u8 c2 = (i & (FN | FC)) | ((i << 2) & FH);
			u8 c3 = c1 & 0x0f;
			switch(i) {
				case 0:
					if (c3 >= 0x0a) {
						c2 |= FH;
						c1 += 6;
						if (c1 < 6) {
							c1 += 0x60;
							c2 |= FC;
							break;
						}
					}
					if (c1 >= 0xa0) {
						c1 += 0x60;
						c2 |= FC;
					}
					break;

				case 1:
					if (c3 >= 0x0a) {
						c2 |= FH;
						c1 += 6;
					}
					c1 += 0x60;
					break;

				case 2:
					if (c1 >= 0x9a) {
						c2 |= FC;
						c1 -= 0x60;
					}
					if (c3 >= 0x0a) {
						c1 -= 6;
					}
					break;

				case 3:
					if (c3 >= 0x0a) {
						c1 -= 6;
					}
					c1 -= 0x60;
					break;

				case 4:
					if (c3 < 0x0a) {
						c2 &= ~FH;
					}
					c1 += 6;
					if (c1 < 6 || c1 >= 0xa0) {
						c1 += 0x60;
						c2 |= FC;
					}
					break;

				case 5:
					if (c3 < 0x0a) {
						c2 &= ~FH;
					}
					c1 += 0x66;
					break;

				case 6:
					if (c1 >= 0x9a) {
						c2 |= FC;
						c1 -= 0x60;
					}
					if (c3 >= 0x06) {
						c2 &= ~FH;
					}
					c1 -= 6;
					break;

				case 7:
					if (c3 >= 0x06) {
						c2 &= ~FH;
					}
					c1 -= 0x66;
					break;
			}
			c2 |= s_szpTbl[c1];
			*p1++ = c1;
			*p2++ = c2;
		}
	}

	p1 = &s_addTbl[0];
	for(i = 0; i < 2; i++) {
		for(j = 0; j < 0x100; j++) {
			for(k = 0; k < 0x100; k++) {
				int s = k + j + i;
				u8 c = (s & FS) | ((s >> 8) & FC);
				if ((s & 0xff) == 0) {
					c |= FZ;
				}
				s = (s8)k + (s8)j + i;
				if (s < -0x80 || s >= 0x80) {
					c |= FPV;
				}
				s = (k & 0x0f) + (j & 0x0f) + i;
				c |= s & FH;
				*p1++ = c;
			}
		}
	}

	p1 = &s_subTbl[0];
	for(i = 0; i < 2; i++) {
		for(j = 0; j < 0x100; j++) {
			for(k = 0; k < 0x100; k++) {
				int s = k - j - i;
				u8 c = (s & FS) | FN | ((s >> 8) & FC);
				if ((s & 0xff) == 0) {
					c |= FZ;
				}
				s = (s8)k - (s8)j - i;
				if (s < -0x80 || s >= 0x80) {
					c |= FPV;
				}
				s = (k & 0x0f) - (j & 0x0f) - i;
				c |= s & FH;
				*p1++ = c;
			}
		}
	}
	return;
}
