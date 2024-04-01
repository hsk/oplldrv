import math

OPLL_DEBUG = False
OPLL_2413_TONE,OPLL_VRC7_TONE,OPLL_281B_TONE = 0,1,2

class OPLL_PATCH:
  def __init__(self,tl=0,fb=0,eg=0,ml=0,ar=0,dr=0,sl=0,rr=0,kr=0,kl=0,am=0,pm=0,ws=0):
    self.TL=tl; self.FB=fb; self.EG=eg; self.ML=ml; self.AR=ar; self.DR=dr
    self.SL=sl; self.RR=rr; self.KR=kr; self.KL=kl; self.AM=am; self.PM=pm; self.WS=ws
  def copy(self):
    return OPLL_PATCH(self.TL,self.FB,self.EG,self.ML,self.AR,self.DR,
                      self.SL,self.RR,self.KR,self.KL,self.AM,self.PM,self.WS)
class OPLL_SLOT:
  def __init__(self,number):
    global _wave_table_map,_null_patch
    self.number = number
    self.type = number % 2
    self.patch = _null_patch
    self.output = [0,0]
    self.wave_table = _wave_table_map[0]
    self.pg_phase = 0
    self.pg_out = 0
    self.pg_keep = 0
    self.blk_fnum = 0
    self.fnum = 0
    self.blk = 0
    self.eg_state = OPLL_EG_STATE.RELEASE
    self.volume = 0
    self.key_flag = 0
    self.sus = 0
    self.tll = 0
    self.rks = 0
    self.eg_rate_h = 0
    self.eg_rate_l = 0
    self.eg_shift = 0
    self.eg_out = EG_MUTE
    self.update_requests = 0
    self.last_eg_state = 0

# mask
def _OPLL_MASK_CH(x): return 1 << x
OPLL_MASK_HH = 1 << 9
OPLL_MASK_CYM = 1 << 10
OPLL_MASK_TOM = 1 << 11
OPLL_MASK_SD = 1 << 12
OPLL_MASK_BD = 1 << 13
OPLL_MASK_RHYTHM = OPLL_MASK_HH | OPLL_MASK_CYM | OPLL_MASK_TOM | OPLL_MASK_SD | OPLL_MASK_BD

class OPLL_RateConv:
  def __init__(self, f_inp, f_out, ch):
    self.ch = ch
    self.timer = 0
    self.f_ratio = f_inp / f_out
    self.buf = [[0] * LW for _ in range(ch)]
    # create sinc_table for positive 0 <= x < LW/2
    self.sinc_table = [0] * (SINC_RESO * LW >> 1)
    for i in range(SINC_RESO * LW >> 1):
      x = i / SINC_RESO
      if f_out < f_inp: # for downsampling:
        self.sinc_table[i] = int((1 << SINC_AMP_BITS) * _windowed_sinc(x / self.f_ratio) / self.f_ratio)
      else: # for upsampling
        self.sinc_table[i] = int((1 << SINC_AMP_BITS) * _windowed_sinc(x))
class OPLL:
  def __init__(self,clk,rate):
    global _table_initialized
    if not _table_initialized: _initializeTables()    
    self.clk = clk
    self.rate = rate
    self.test_flag = 0
    self.lfo_am = 0
    self.short_noise = 0
    self.patch = [OPLL_PATCH() for _ in range(19 * 2)]
    self.pan = [3]*16
    self.pan_fine = [[0.0,0.0] for _ in range(16)]
    self.mix_out = [0,0]
    self.conv = None
    OPLL_reset(self)
    OPLL_setChipType(self, 0)
    OPLL_resetPatch(self, 0)
    
_PI_ = 3.14159265358979323846264338327950288

OPLL_TONE_NUM = 3
_default_inst = [
  bytearray([
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, # 0: User
    0x71,0x61,0x1e,0x17,0xd0,0x78,0x00,0x17, # 1: Violin
    0x13,0x41,0x1a,0x0d,0xd8,0xf7,0x23,0x13, # 2: Guitar
    0x13,0x01,0x99,0x00,0xf2,0xc4,0x21,0x23, # 3: Piano
    0x11,0x61,0x0e,0x07,0x8d,0x64,0x70,0x27, # 4: Flute
    0x32,0x21,0x1e,0x06,0xe1,0x76,0x01,0x28, # 5: Clarinet
    0x31,0x22,0x16,0x05,0xe0,0x71,0x00,0x18, # 6: Oboe
    0x21,0x61,0x1d,0x07,0x82,0x81,0x11,0x07, # 7: Trumpet
    0x33,0x21,0x2d,0x13,0xb0,0x70,0x00,0x07, # 8: Organ
    0x61,0x61,0x1b,0x06,0x64,0x65,0x10,0x17, # 9: Horn
    0x41,0x61,0x0b,0x18,0x85,0xf0,0x81,0x07, # A: Synthesizer
    0x33,0x01,0x83,0x11,0xea,0xef,0x10,0x04, # B: Harpsichord
    0x17,0xc1,0x24,0x07,0xf8,0xf8,0x22,0x12, # C: Vibraphone
    0x61,0x50,0x0c,0x05,0xd2,0xf5,0x40,0x42, # D: Synthsizer Bass
    0x01,0x01,0x55,0x03,0xe9,0x90,0x03,0x02, # E: Acoustic Bass
    0x41,0x41,0x89,0x03,0xf1,0xe4,0xc0,0x13, # F: Electric Guitar
    0x01,0x01,0x18,0x0f,0xdf,0xf8,0x6a,0x6d, # R: Bass Drum (from VRC7)
    0x01,0x01,0x00,0x00,0xc8,0xd8,0xa7,0x68, # R: High-Hat(M) / Snare Drum(C) (from VRC7)
    0x05,0x01,0x00,0x00,0xf8,0xaa,0x59,0x55, # R: Tom-tom(M) / Top Cymbal(C) (from VRC7)
  ]),
  bytearray([ # VRC7 presets from Nuke.YKT
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x03,0x21,0x05,0x06,0xe8,0x81,0x42,0x27,
    0x13,0x41,0x14,0x0d,0xd8,0xf6,0x23,0x12,
    0x11,0x11,0x08,0x08,0xfa,0xb2,0x20,0x12,
    0x31,0x61,0x0c,0x07,0xa8,0x64,0x61,0x27,
    0x32,0x21,0x1e,0x06,0xe1,0x76,0x01,0x28,
    0x02,0x01,0x06,0x00,0xa3,0xe2,0xf4,0xf4,
    0x21,0x61,0x1d,0x07,0x82,0x81,0x11,0x07,
    0x23,0x21,0x22,0x17,0xa2,0x72,0x01,0x17,
    0x35,0x11,0x25,0x00,0x40,0x73,0x72,0x01,
    0xb5,0x01,0x0f,0x0F,0xa8,0xa5,0x51,0x02,
    0x17,0xc1,0x24,0x07,0xf8,0xf8,0x22,0x12,
    0x71,0x23,0x11,0x06,0x65,0x74,0x18,0x16,
    0x01,0x02,0xd3,0x05,0xc9,0x95,0x03,0x02,
    0x61,0x63,0x0c,0x00,0x94,0xC0,0x33,0xf6,
    0x21,0x72,0x0d,0x00,0xc1,0xd5,0x56,0x06,
    0x01,0x01,0x18,0x0f,0xdf,0xf8,0x6a,0x6d,
    0x01,0x01,0x00,0x00,0xc8,0xd8,0xa7,0x68,
    0x05,0x01,0x00,0x00,0xf8,0xaa,0x59,0x55,
  ]),
  bytearray([ # YMF281B presets
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, # 0: User
    0x62,0x21,0x1a,0x07,0xf0,0x6f,0x00,0x16, # 1: Electric Strings (form Chabin's patch)
    0x40,0x10,0x45,0x00,0xf6,0x83,0x73,0x63, # 2: Bow Wow (based on plgDavid's patch, KSL fixed)
    0x13,0x01,0x99,0x00,0xf2,0xc3,0x21,0x23, # 3: Electric Guitar (similar to YM2413 but different DR(C))
    0x01,0x61,0x0b,0x0f,0xf9,0x64,0x70,0x17, # 4: Organ (based on Chabin, TL/DR fixed)
    0x32,0x21,0x1e,0x06,0xe1,0x76,0x01,0x28, # 5: Clarinet (identical to YM2413)
    0x60,0x01,0x82,0x0e,0xf9,0x61,0x20,0x27, # 6: Saxophone (based on plgDavid, PM/EG fixed)
    0x21,0x61,0x1c,0x07,0x84,0x81,0x11,0x07, # 7: Trumpet (similar to YM2413 but different TL/DR(M))
    0x37,0x32,0xc9,0x01,0x66,0x64,0x40,0x28, # 8: Street Organ (from Chabin)
    0x01,0x21,0x07,0x03,0xa5,0x71,0x51,0x07, # 9: Synth Brass (based on Chabin, TL fixed)
    0x06,0x01,0x5e,0x07,0xf3,0xf3,0xf6,0x13, # A: Electric Piano (based on Chabin, DR/RR/KR fixed)
    0x00,0x00,0x18,0x06,0xf5,0xf3,0x20,0x23, # B: Bass (based on Chabin, EG fixed) 
    0x17,0xc1,0x24,0x07,0xf8,0xf8,0x22,0x12, # C: Vibraphone (identical to YM2413)
    0x35,0x64,0x00,0x00,0xff,0xf3,0x77,0xf5, # D: Chimes (from plgDavid)
    0x11,0x31,0x00,0x07,0xdd,0xf3,0xff,0xfb, # E: Tom Tom II (from plgDavid)
    0x3a,0x21,0x00,0x07,0x80,0x84,0x0f,0xf5, # F: Noise (based on plgDavid, AR fixed)
    0x01,0x01,0x18,0x0f,0xdf,0xf8,0x6a,0x6d, # R: Bass Drum (identical to YM2413)
    0x01,0x01,0x00,0x00,0xc8,0xd8,0xa7,0x68, # R: High-Hat(M) / Snare Drum(C) (identical to YM2413)
    0x05,0x01,0x00,0x00,0xf8,0xaa,0x59,0x55, # R: Tom-tom(M) / Top Cymbal(C) (identical to YM2413)
  ])
]

# sine table
PG_BITS = 10 # 2^10 = 1024 length sine table
PG_WIDTH = 1 << PG_BITS
# phase increment counter
DP_BITS = 19
DP_WIDTH = 1 << DP_BITS
DP_BASE_BITS = DP_BITS - PG_BITS

# dynamic range of envelope output
EG_STEP = 0.375
EG_BITS = 7
EG_MUTE = (1 << EG_BITS) - 1
EG_MAX = EG_MUTE - 4

# dynamic range of total level
TL_STEP = 0.75
TL_BITS = 6

# dynamic range of sustine level
SL_STEP = 3.0
SL_BITS = 4

# damper speed before key-on. key-scale affects.
DAMPER_RATE = 12

def _TL2EG(d): return d << 1


# _exp_table[x] = round((exp2((double)x / 256.0) - 1) * 1024)
_exp_table = [
  0,    3,    6,    8,    11,   14,   17,   20,   22,   25,   28,   31,   34,   37,   40,   42,
  45,   48,   51,   54,   57,   60,   63,   66,   69,   72,   75,   78,   81,   84,   87,   90,
  93,   96,   99,   102,  105,  108,  111,  114,  117,  120,  123,  126,  130,  133,  136,  139,
  142,  145,  148,  152,  155,  158,  161,  164,  168,  171,  174,  177,  181,  184,  187,  190,
  194,  197,  200,  204,  207,  210,  214,  217,  220,  224,  227,  231,  234,  237,  241,  244,
  248,  251,  255,  258,  262,  265,  268,  272,  276,  279,  283,  286,  290,  293,  297,  300,
  304,  308,  311,  315,  318,  322,  326,  329,  333,  337,  340,  344,  348,  352,  355,  359,
  363,  367,  370,  374,  378,  382,  385,  389,  393,  397,  401,  405,  409,  412,  416,  420,
  424,  428,  432,  436,  440,  444,  448,  452,  456,  460,  464,  468,  472,  476,  480,  484,
  488,  492,  496,  501,  505,  509,  513,  517,  521,  526,  530,  534,  538,  542,  547,  551,
  555,  560,  564,  568,  572,  577,  581,  585,  590,  594,  599,  603,  607,  612,  616,  621,
  625,  630,  634,  639,  643,  648,  652,  657,  661,  666,  670,  675,  680,  684,  689,  693,
  698,  703,  708,  712,  717,  722,  726,  731,  736,  741,  745,  750,  755,  760,  765,  770,
  774,  779,  784,  789,  794,  799,  804,  809,  814,  819,  824,  829,  834,  839,  844,  849,
  854,  859,  864,  869,  874,  880,  885,  890,  895,  900,  906,  911,  916,  921,  927,  932,
  937,  942,  948,  953,  959,  964,  969,  975,  980,  986,  991,  996, 1002, 1007, 1013, 1018
]
# _fullsin_table[x] = round(-log2(sin((x + 0.5) * PI / (int(PG_WIDTH / 4)) / 2)) * 256)
_fullsin_table = [
  2137, 1731, 1543, 1419, 1326, 1252, 1190, 1137, 1091, 1050, 1013, 979,  949,  920,  894,  869, 
  846,  825,  804,  785,  767,  749,  732,  717,  701,  687,  672,  659,  646,  633,  621,  609, 
  598,  587,  576,  566,  556,  546,  536,  527,  518,  509,  501,  492,  484,  476,  468,  461,
  453,  446,  439,  432,  425,  418,  411,  405,  399,  392,  386,  380,  375,  369,  363,  358,  
  352,  347,  341,  336,  331,  326,  321,  316,  311,  307,  302,  297,  293,  289,  284,  280,
  276,  271,  267,  263,  259,  255,  251,  248,  244,  240,  236,  233,  229,  226,  222,  219, 
  215,  212,  209,  205,  202,  199,  196,  193,  190,  187,  184,  181,  178,  175,  172,  169, 
  167,  164,  161,  159,  156,  153,  151,  148,  146,  143,  141,  138,  136,  134,  131,  129,  
  127,  125,  122,  120,  118,  116,  114,  112,  110,  108,  106,  104,  102,  100,  98,   96,   
  94,   92,   91,   89,   87,   85,   83,   82,   80,   78,   77,   75,   74,   72,   70,   69,
  67,   66,   64,   63,   62,   60,   59,   57,   56,   55,   53,   52,   51,   49,   48,   47,  
  46,   45,   43,   42,   41,   40,   39,   38,   37,   36,   35,   34,   33,   32,   31,   30,  
  29,   28,   27,   26,   25,   24,   23,   23,   22,   21,   20,   20,   19,   18,   17,   17,   
  16,   15,   15,   14,   13,   13,   12,   12,   11,   10,   10,   9,    9,    8,    8,    7,    
  7,    7,    6,    6,    5,    5,    5,    4,    4,    4,    3,    3,    3,    2,    2,    2,
  2,    1,    1,    1,    1,    1,    1,    1,    0,    0,    0,    0,    0,    0,    0,    0,
]

halfsin_table = [0] * PG_WIDTH
_wave_table_map = [_fullsin_table, halfsin_table]

# pitch modulator
# offset to fnum, rough approximation of 14 cents depth.
_pm_table = [
    [0, 0, 0, 0, 0,  0,  0,  0], # fnum = 000xxxxxx
    [0, 0, 1, 0, 0,  0, -1,  0], # fnum = 001xxxxxx
    [0, 1, 2, 1, 0, -1, -2, -1], # fnum = 010xxxxxx
    [0, 1, 3, 1, 0, -1, -3, -1], # fnum = 011xxxxxx
    [0, 2, 4, 2, 0, -2, -4, -2], # fnum = 100xxxxxx
    [0, 2, 5, 2, 0, -2, -5, -2], # fnum = 101xxxxxx
    [0, 3, 6, 3, 0, -3, -6, -3], # fnum = 110xxxxxx
    [0, 3, 7, 3, 0, -3, -7, -3], # fnum = 111xxxxxx
]

# amplitude lfo table
# The following envelop pattern is verified on real YM2413.
# each element repeates 64 cycles
_am_table = bytearray([0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  1,  1,  1,  #
            2,  2,  2,  2,  2,  2,  2,  2,  3,  3,  3,  3,  3,  3,  3,  3,  #
            4,  4,  4,  4,  4,  4,  4,  4,  5,  5,  5,  5,  5,  5,  5,  5,  #
            6,  6,  6,  6,  6,  6,  6,  6,  7,  7,  7,  7,  7,  7,  7,  7,  #
            8,  8,  8,  8,  8,  8,  8,  8,  9,  9,  9,  9,  9,  9,  9,  9,  #
            10, 10, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, #
            12, 12, 12, 12, 12, 12, 12, 12,                                 #
            13, 13, 13,                                                     #
            12, 12, 12, 12, 12, 12, 12, 12,                                 #
            11, 11, 11, 11, 11, 11, 11, 11, 10, 10, 10, 10, 10, 10, 10, 10, #
            9,  9,  9,  9,  9,  9,  9,  9,  8,  8,  8,  8,  8,  8,  8,  8,  #
            7,  7,  7,  7,  7,  7,  7,  7,  6,  6,  6,  6,  6,  6,  6,  6,  #
            5,  5,  5,  5,  5,  5,  5,  5,  4,  4,  4,  4,  4,  4,  4,  4,  #
            3,  3,  3,  3,  3,  3,  3,  3,  2,  2,  2,  2,  2,  2,  2,  2,  #
            1,  1,  1,  1,  1,  1,  1,  1,  0,  0,  0,  0,  0,  0,  0])
# envelope decay increment step table
# based on andete's research
_eg_step_tables = [
    bytearray([0, 1, 0, 1, 0, 1, 0, 1]),
    bytearray([0, 1, 0, 1, 1, 1, 0, 1]),
    bytearray([0, 1, 1, 1, 0, 1, 1, 1]),
    bytearray([0, 1, 1, 1, 1, 1, 1, 1]),
]
class OPLL_EG_STATE:
  ATTACK = 1
  DECAY = 2
  SUSTAIN = 3
  RELEASE = 4
  DAMP = 5
  UNKNOWN = 6

_ml_table = bytearray([1,     1 * 2, 2 * 2,  3 * 2,  4 * 2,  5 * 2,  6 * 2,  7 * 2,
                     8 * 2, 9 * 2, 10 * 2, 10 * 2, 12 * 2, 12 * 2, 15 * 2, 15 * 2])

def _dB2(x): return x*2

_kl_table = [_dB2(0.000),  _dB2(9.000),  _dB2(12.000), _dB2(13.875), _dB2(15.000), _dB2(16.125),
             _dB2(16.875), _dB2(17.625), _dB2(18.000), _dB2(18.750), _dB2(19.125), _dB2(19.500),
             _dB2(19.875), _dB2(20.250), _dB2(20.625), _dB2(21.000)]

_tll_table = [ [[0]*4 for _ in range(1 << TL_BITS)] for _ in range(8 * 16)]
_rks_table = [[0]*2 for _ in range(8 * 2)]
_null_patch = OPLL_PATCH()
_default_patch = [[[OPLL_PATCH(),OPLL_PATCH()] for _ in range((16 + 3))] for _ in range(OPLL_TONE_NUM)]

#***************************************************
#
#           Internal Sample Rate Converter
#
#***************************************************
# Note: to disable internal rate converter, set clock/72 to output sampling rate.

#
# LW is truncate length of _sinc(x) calculation.
# Lower LW is faster, higher LW results better quality.
# LW must be a non-zero positive even number, no upper limit.
# LW=16 or greater is recommended when upsampling.
# LW=8 is practically okay for downsampling.
#
LW = 16

# resolution of _sinc(x) table. _sinc(x) where 0.0<=x<1.0 corresponds to sinc_table[0...SINC_RESO-1]
SINC_RESO = 256
SINC_AMP_BITS = 12

# def _hamming(x): return 0.54 - 0.46 * cos(2 * PI * x)
def _blackman(x): return 0.42 - 0.5 * math.cos(2 * _PI_ * x) + 0.08 * math.cos(4 * _PI_ * x)
def _sinc(x): return (1.0 if x == 0.0 else math.sin(_PI_ * x) / (_PI_ * x))
def _windowed_sinc(x): return _blackman(0.5 + 0.5 * x / (LW >> 1)) * _sinc(x)

def _lookup_sinc_table(table, x):
  index = int(x * SINC_RESO)
  if index < 0: index = -index
  return table[min((SINC_RESO * LW >> 1) - 1, index)]

def OPLL_RateConv_reset(conv):
  conv.timer = 0
  for i in range(conv.ch):
    conv.buf[i][:] = [0]*LW

# put original data to this converter at f_inp.
def OPLL_RateConv_putData(conv, ch, data):
  buf = conv.buf[ch]
  for i in range(LW - 1):
    buf[i] = buf[i + 1]
  buf[LW - 1] = data

# get resampled data from this converter at f_out.
# this function must be called f_out / f_inp times per one putData call.
def OPLL_RateConv_getData(conv, ch):
  buf = conv.buf[ch]
  sum = 0
  conv.timer += conv.f_ratio
  dn = conv.timer - math.floor(conv.timer)
  conv.timer = dn
  for k in range(LW):
    x = (k - ((LW>>1) - 1)) - dn
    sum += buf[k] * _lookup_sinc_table(conv.sinc_table, x)
  return sum >> SINC_AMP_BITS

#***************************************************
#
#                  Create tables
#
#***************************************************

def _makeSinTable():
  global _fullsin_table, _fullsin_table
  while len(_fullsin_table) < PG_WIDTH: _fullsin_table.append(0)
  for x in range(PG_WIDTH >> 2):
    _fullsin_table[(PG_WIDTH >> 2) + x] = _fullsin_table[(PG_WIDTH >> 2) - x - 1]
  for x in range(PG_WIDTH >> 1):
    _fullsin_table[PG_WIDTH >> 1 + x] = 0x8000 | _fullsin_table[x]
  while len(halfsin_table) < PG_WIDTH: halfsin_table.append(0)
  for x in range(PG_WIDTH >> 1):
    halfsin_table[x] = _fullsin_table[x]
  for x in range(PG_WIDTH >> 1, PG_WIDTH):
    halfsin_table[x] = 0xfff

def _makeTllTable():
  for fnum in range(16):
    for block in range(8):
      for TL in range(64):
        for KL in range(4):
          if KL == 0:
            _tll_table[(block << 4) | fnum][TL][KL] = _TL2EG(TL)
          else:
            tmp = int(_kl_table[fnum] - _dB2(3.000) * (7 - block))
            if tmp <= 0:
              _tll_table[(block << 4) | fnum][TL][KL] = _TL2EG(TL)
            else:
              _tll_table[(block << 4) | fnum][TL][KL] = int((tmp >> (3 - KL)) / EG_STEP) + _TL2EG(TL)

def _makeRksTable():
  for fnum8 in range(2):
    for block in range(8):
      _rks_table[(block << 1) | fnum8][1] = (block << 1) + fnum8
      _rks_table[(block << 1) | fnum8][0] = block >> 1

def _makeDefaultPatch():
  global _default_patch
  for i in range(OPLL_TONE_NUM):
    for j in range(19):
      OPLL_getDefaultPatch(i, j, _default_patch[i][j])

_table_initialized = 0

def _initializeTables():
  global _table_initialized
  _makeTllTable()
  _makeRksTable()
  _makeSinTable()
  _makeDefaultPatch()
  _table_initialized = 1

#*********************************************************
#
#                      Synthesizing
#
#*********************************************************
SLOT_BD1 = 12
SLOT_BD2 = 13
SLOT_HH = 14
SLOT_SD = 15
SLOT_TOM = 16
SLOT_CYM = 17

# utility macros
def _MOD(o, x): return o.slot[x << 1]
def _CAR(o, x): return o.slot[(x << 1) | 1]
def _BIT(s, b): return (s >> b) & 1

def _debug_print_patch(slot):
  p = slot.patch
  print("[slot#{:d} am:{:d} pm:{:d} eg:{:d} kr:{:d} ml:{:d} kl:{:d} tl:{:d} ws:{:d} fb:{:d} A:{:d} D:{:d} S:{:d} R:{:d}]".format(
        slot.number,
         p.AM, p.PM, p.EG, p.KR, p.ML,
         p.KL, p.TL, p.WS, p.FB,
         p.AR, p.DR, p.SL, p.RR))

def _debug_eg_state_name(slot):
  match slot.eg_state:
    case OPLL_EG_STATE.ATTACK:  return "attack"
    case OPLL_EG_STATE.DECAY:   return "decay"
    case OPLL_EG_STATE.SUSTAIN: return "sustain"
    case OPLL_EG_STATE.RELEASE: return "release"
    case OPLL_EG_STATE.DAMP:    return "damp"
    case _:                     return "unknown"

def _debug_print_slot_info(slot):
  name = _debug_eg_state_name(slot)
  print("[slot#{:d} state:{} fnum:{:03x} rate:{:d}-{:d}]".format(
    slot.number, name, slot.blk_fnum, slot.eg_rate_h, slot.eg_rate_l))
  _debug_print_patch(slot)
  #fflush(stdout)

def _get_parameter_rate(slot):
  if (slot.type & 1) == 0 and slot.key_flag == 0: return 0
  match slot.eg_state:
    case OPLL_EG_STATE.ATTACK:
      return slot.patch.AR
    case OPLL_EG_STATE.DECAY:
      return slot.patch.DR
    case OPLL_EG_STATE.SUSTAIN:
      return 0 if slot.patch.EG else slot.patch.RR
    case OPLL_EG_STATE.RELEASE:
      if slot.sus_flag:
        return 5
      elif slot.patch.EG:
        return slot.patch.RR
      else:
        return 7
    case OPLL_EG_STATE.DAMP:
      return DAMPER_RATE
    case _:
      return 0

UPDATE_WS = 1
UPDATE_TLL = 2
UPDATE_RKS = 4
UPDATE_EG = 8
UPDATE_ALL = 255

def _request_update(slot, flag): slot.update_requests |= flag

def _commit_slot_update(slot):
  global _wave_table_map, _tll_table, _rks_table
  if OPLL_DEBUG:
    if (slot.last_eg_state != slot.eg_state):
      _debug_print_slot_info(slot)
      slot.last_eg_state = slot.eg_state
  if (slot.update_requests & UPDATE_WS):
    slot.wave_table = _wave_table_map[slot.patch.WS]
  if (slot.update_requests & UPDATE_TLL):
    if (slot.type & 1) == 0:
      slot.tll = _tll_table[slot.blk_fnum >> 5][slot.patch.TL][slot.patch.KL]
    else:
      slot.tll = _tll_table[slot.blk_fnum >> 5][slot.volume][slot.patch.KL]
  if (slot.update_requests & UPDATE_RKS):
    slot.rks = _rks_table[slot.blk_fnum >> 8][slot.patch.KR]
  if (slot.update_requests & (UPDATE_RKS | UPDATE_EG)):
    p_rate = _get_parameter_rate(slot)
    if (p_rate == 0):
      slot.eg_shift = 0
      slot.eg_rate_h = 0
      slot.eg_rate_l = 0
      return
    slot.eg_rate_h = min(15, p_rate + (slot.rks >> 2))
    slot.eg_rate_l = slot.rks & 3
    if (slot.eg_state == OPLL_EG_STATE.ATTACK):
      slot.eg_shift = 13 - slot.eg_rate_h if 0 < slot.eg_rate_h and slot.eg_rate_h < 12 else 0
    else:
      slot.eg_shift = 13 - slot.eg_rate_h if slot.eg_rate_h < 13 else 0
  slot.update_requests = 0

def _slotOn(opll, i):
  slot = opll.slot[i]
  slot.key_flag = 1
  slot.eg_state = OPLL_EG_STATE.DAMP
  _request_update(slot, UPDATE_EG)

def _slotOff(opll, i):
  slot = opll.slot[i]
  slot.key_flag = 0
  if slot.type & 1:
    slot.eg_state = OPLL_EG_STATE.RELEASE
    _request_update(slot, UPDATE_EG)

def _update_key_status(opll):
  r14 = opll.reg[0x0e]
  rhythm_mode = _BIT(r14, 5)
  new_slot_key_status = 0
  for ch in range(9):
    if opll.reg[0x20 + ch] & 0x10:
      new_slot_key_status |= 3 << (ch * 2)

  if rhythm_mode:
    if r14 & 0x10: new_slot_key_status |= 3 << SLOT_BD1
    if r14 & 0x01: new_slot_key_status |= 1 << SLOT_HH
    if r14 & 0x08: new_slot_key_status |= 1 << SLOT_SD
    if r14 & 0x04: new_slot_key_status |= 1 << SLOT_TOM
    if r14 & 0x02: new_slot_key_status |= 1 << SLOT_CYM

  updated_status = opll.slot_key_status ^ new_slot_key_status
  if updated_status:
    for i in range(18):
      if _BIT(updated_status, i):
        if _BIT(new_slot_key_status, i): _slotOn(opll, i)
        else:                           _slotOff(opll, i)
  opll.slot_key_status = new_slot_key_status

def _set_patch(opll, ch, num):
  opll.patch_number[ch] = num
  _MOD(opll, ch).patch = opll.patch[num * 2 + 0]
  _CAR(opll, ch).patch = opll.patch[num * 2 + 1]
  _request_update(_MOD(opll, ch), UPDATE_ALL)
  _request_update(_CAR(opll, ch), UPDATE_ALL)

def _set_sus_flag(opll, ch, flag):
  _CAR(opll, ch).sus_flag = flag
  _request_update(_CAR(opll, ch), UPDATE_EG)
  if (_MOD(opll, ch).type & 1):
    _MOD(opll, ch).sus_flag = flag
    _request_update(_MOD(opll, ch), UPDATE_EG)

# set volume ( volume : 6bit, register value << 2 )
def _set_volume(opll, ch, volume):
  _CAR(opll, ch).volume = volume
  _request_update(_CAR(opll, ch), UPDATE_TLL)

def _set_slot_volume(slot, volume):
  slot.volume = volume
  _request_update(slot, UPDATE_TLL)

# set f-Nnmber ( fnum : 9bit )
def _set_fnumber(opll, ch, fnum):
  car = _CAR(opll, ch)
  mod = _MOD(opll, ch)
  car.fnum = fnum
  car.blk_fnum = (car.blk_fnum & 0xe00) | (fnum & 0x1ff)
  mod.fnum = fnum
  mod.blk_fnum = (mod.blk_fnum & 0xe00) | (fnum & 0x1ff)
  _request_update(car, UPDATE_EG | UPDATE_RKS | UPDATE_TLL)
  _request_update(mod, UPDATE_EG | UPDATE_RKS | UPDATE_TLL)

# set block data (blk : 3bit )
def _set_block(opll, ch, blk):
  car = _CAR(opll, ch)
  mod = _MOD(opll, ch)
  car.blk = blk
  car.blk_fnum = ((blk & 7) << 9) | (car.blk_fnum & 0x1ff)
  mod.blk = blk
  mod.blk_fnum = ((blk & 7) << 9) | (mod.blk_fnum & 0x1ff)
  _request_update(car, UPDATE_EG | UPDATE_RKS | UPDATE_TLL)
  _request_update(mod, UPDATE_EG | UPDATE_RKS | UPDATE_TLL)

def _update_rhythm_mode(opll):
  new_rhythm_mode = (opll.reg[0x0e] >> 5) & 1
  if opll.rhythm_mode != new_rhythm_mode:
    if new_rhythm_mode:
      opll.slot[SLOT_HH].type = 3
      opll.slot[SLOT_HH].pg_keep = 1
      opll.slot[SLOT_SD].type = 3
      opll.slot[SLOT_TOM].type = 3
      opll.slot[SLOT_CYM].type = 3
      opll.slot[SLOT_CYM].pg_keep = 1
      _set_patch(opll, 6, 16)
      _set_patch(opll, 7, 17)
      _set_patch(opll, 8, 18)
      _set_slot_volume(opll.slot[SLOT_HH], ((opll.reg[0x37] >> 4) & 15) << 2)
      _set_slot_volume(opll.slot[SLOT_TOM], ((opll.reg[0x38] >> 4) & 15) << 2)
    else:
      opll.slot[SLOT_HH].type = 0
      opll.slot[SLOT_HH].pg_keep = 0
      opll.slot[SLOT_SD].type = 1
      opll.slot[SLOT_TOM].type = 0
      opll.slot[SLOT_CYM].type = 1
      opll.slot[SLOT_CYM].pg_keep = 0
      _set_patch(opll, 6, opll.reg[0x36] >> 4)
      _set_patch(opll, 7, opll.reg[0x37] >> 4)
      _set_patch(opll, 8, opll.reg[0x38] >> 4)

  opll.rhythm_mode = new_rhythm_mode

def _update_ampm(opll):
  global _am_table
  if opll.test_flag & 2:
    opll.pm_phase = 0
    opll.am_phase = 0
  else:
    opll.pm_phase += 1024 if opll.test_flag & 8 else 1
    opll.am_phase += 64 if opll.test_flag & 8 else 1
  #print(f"am {opll['am_phase'] >> 6}")
  opll.lfo_am = _am_table[(opll.am_phase >> 6) % len(_am_table)]

def _update_noise(opll, cycle):
  for i in range (cycle):
    if opll.noise & 1:
      opll.noise ^= 0x800200
    opll.noise >>= 1

def _update_short_noise(opll):
  pg_hh = opll.slot[SLOT_HH].pg_out
  pg_cym = opll.slot[SLOT_CYM].pg_out
  h_bit2 = _BIT(pg_hh, PG_BITS - 8)
  h_bit7 = _BIT(pg_hh, PG_BITS - 3)
  h_bit3 = _BIT(pg_hh, PG_BITS - 7)
  c_bit3 = _BIT(pg_cym, PG_BITS - 7)
  c_bit5 = _BIT(pg_cym, PG_BITS - 5)
  opll.short_noise = (h_bit2 ^ h_bit7) | (h_bit3 ^ c_bit5) | (c_bit3 ^ c_bit5)

def _calc_phase(slot, pm_phase, reset):
  global _pm_table, _ml_table
  pm = _pm_table[(slot.fnum >> 6) & 7][(pm_phase >> 10) & 7] if slot.patch.PM else 0
  if reset: slot.pg_phase = 0
  slot.pg_phase += (((slot.fnum & 0x1ff) * 2 + pm) * _ml_table[slot.patch.ML]) << slot.blk >> 2
  slot.pg_phase &= (DP_WIDTH - 1)
  slot.pg_out = slot.pg_phase >> DP_BASE_BITS

def _lookup_attack_step(slot, counter):
  global _eg_step_tables
  match slot.eg_rate_h:
    case 12:
      index = (counter & 0xc) >> 1
      return 4 - _eg_step_tables[slot.eg_rate_l][index]
    case 13:
      index = (counter & 0xc) >> 1
      return 3 - _eg_step_tables[slot.eg_rate_l][index]
    case 14:
      index = (counter & 0xc) >> 1
      return 2 - _eg_step_tables[slot.eg_rate_l][index]
    case 0 | 15:
      return 0
    case _:
      index = counter >> slot.eg_shift
      return 4 if _eg_step_tables[slot.eg_rate_l][index & 7] else 0

def _lookup_decay_step(slot, counter):
  global _eg_step_tables
  match slot.eg_rate_h:
    case 0:
      return 0
    case 13:
      index = ((counter & 0xc) >> 1) | (counter & 1)
      return _eg_step_tables[slot.eg_rate_l][index]
    case 14:
      index = ((counter & 0xc) >> 1)
      return _eg_step_tables[slot.eg_rate_l][index] + 1
    case 15:
      return 2
    case _:
      index = counter >> slot.eg_shift
      return _eg_step_tables[slot.eg_rate_l][index & 7]

def _start_envelope(slot):
  if (min(15, slot.patch.AR + (slot.rks >> 2)) == 15):
    slot.eg_state = OPLL_EG_STATE.DECAY
    slot.eg_out = 0
  else:
    slot.eg_state = OPLL_EG_STATE.ATTACK
  _request_update(slot, UPDATE_EG)

def _calc_envelope(slot, buddy, eg_counter, test):
  mask = (1 << slot.eg_shift) - 1
  if (slot.eg_state == OPLL_EG_STATE.ATTACK):
    if 0 < slot.eg_out and 0 < slot.eg_rate_h and (eg_counter & mask & ~3) == 0:
      s = _lookup_attack_step(slot, eg_counter)
      if 0 < s:
        slot.eg_out = max(0, (int(slot.eg_out) - (slot.eg_out >> s) - 1))
  elif slot.eg_rate_h > 0 and (eg_counter & mask) == 0:
    slot.eg_out = min(EG_MUTE, slot.eg_out + _lookup_decay_step(slot, eg_counter))

  match slot.eg_state:
    case OPLL_EG_STATE.DAMP:
      # OPLL_EG_STATE.DAMP to OPLL_EG_STATE.ATTACK transition is occured when the envelope reaches EG_MAX (max attenuation but it's not mute).
      # Do not forget to check (eg_counter & mask) == 0 to synchronize it with the progress of the envelope.
      if slot.eg_out >= EG_MAX and (eg_counter & mask) == 0:
        _start_envelope(slot)
        if slot.type & 1:
          if not slot.pg_keep: slot.pg_phase = 0
          if buddy and not buddy.pg_keep: buddy.pg_phase = 0
    case OPLL_EG_STATE.ATTACK:
      if (slot.eg_out == 0):
        slot.eg_state = OPLL_EG_STATE.DECAY
        _request_update(slot, UPDATE_EG)
    case OPLL_EG_STATE.DECAY:
      # OPLL_EG_STATE.DECAY to OPLL_EG_STATE.SUSTAIN transition must be checked at every cycle regardless of the conditions of the envelope rate and
      # counter. i.e. the transition is not synchronized with the progress of the envelope.
      if ((slot.eg_out >> 3) == slot.patch.SL):
        slot.eg_state = OPLL_EG_STATE.SUSTAIN
        _request_update(slot, UPDATE_EG)
    case OPLL_EG_STATE.SUSTAIN | OPLL_EG_STATE.RELEASE | _: pass
  if test:
    slot.eg_out = 0

def _update_slots(opll):
  opll.eg_counter+=1
  for i in range (18):
    slot = opll.slot[i]
    buddy = None
    if (slot.type == 0): buddy = opll.slot[i + 1]
    if (slot.type == 1): buddy = opll.slot[i - 1]
    if (slot.update_requests): _commit_slot_update(slot)
    _calc_envelope(slot, buddy, opll.eg_counter, opll.test_flag & 1)
    _calc_phase(slot, opll.pm_phase, opll.test_flag & 4)

# output: -4095...4095
def _lookup_exp_table(i):
  global _exp_table
  # from andete's expression
  t = (_exp_table[(i & 0xff) ^ 0xff] + 1024)
  res = t >> ((i & 0x7f00) >> 8)
  return (~res if i & 0x8000 else res) << 1

def _to_linear(h, slot, am):
  if slot.eg_out > EG_MAX: return 0
  att = min(EG_MUTE, (slot.eg_out + slot.tll + am)) << 4
  return _lookup_exp_table(h + att)

def _calc_slot_car(opll, ch, fm):
  slot = _CAR(opll, ch)
  am = opll.lfo_am if slot.patch.AM else 0
  slot.output[1] = slot.output[0]
  slot.output[0] = _to_linear(slot.wave_table[(slot.pg_out + 2 * (fm >> 1)) & (PG_WIDTH - 1)], slot, am)
  return slot.output[0]

def _calc_slot_mod(opll, ch):
  slot = _MOD(opll, ch)
  fm = (slot.output[1] + slot.output[0]) >> (9 - slot.patch.FB) if slot.patch.FB > 0 else 0
  am = opll.lfo_am if slot.patch.AM else 0
  slot.output[1] = slot.output[0]
  slot.output[0] = _to_linear(slot.wave_table[(slot.pg_out + fm) & (PG_WIDTH - 1)], slot, am)
  return slot.output[0]

def _calc_slot_tom(opll):
  slot = _MOD(opll, 8)
  return _to_linear(slot.wave_table[slot.pg_out], slot, 0)

# Specify phase offset directly based on 10-bit (1024-length) sine table
def _PD(phase): return (phase >> (10 - PG_BITS)) if (PG_BITS < 10) else (phase << (PG_BITS - 10))

def _calc_slot_snare(opll):
  slot = _CAR(opll, 7)
  if (_BIT(slot.pg_out, PG_BITS - 2)):
    phase = _PD(0x300) if(opll.noise & 1) else _PD(0x200)
  else:
    phase = _PD(0x0) if(opll.noise & 1) else _PD(0x100)
  return _to_linear(slot.wave_table[phase], slot, 0)

def _calc_slot_cym(opll):
  slot = _CAR(opll, 8)
  phase = _PD(0x300) if opll.short_noise else _PD(0x100)
  return _to_linear(slot.wave_table[phase], slot, 0)

def _calc_slot_hat(opll):
  slot = _MOD(opll, 7)
  if opll.short_noise:
    phase = _PD(0x2d0) if (opll.noise & 1) else _PD(0x234)
  else:
    phase = _PD(0x34) if (opll.noise & 1) else _PD(0xd0)
  return _to_linear(slot.wave_table[phase], slot, 0)

def _MO(x): return -x >> 1
def _RO(x): return x

def _update_output(opll):
  _update_ampm(opll)
  _update_short_noise(opll)
  _update_slots(opll)
  out = opll.ch_out
  # CH1-6
  for i in range(6):
    if not (opll.mask & _OPLL_MASK_CH(i)):
      out[i] = _MO(_calc_slot_car(opll, i, _calc_slot_mod(opll, i)))
  # CH7
  if not opll.rhythm_mode:
    if not (opll.mask & _OPLL_MASK_CH(6)):
      out[6] = _MO(_calc_slot_car(opll, 6, _calc_slot_mod(opll, 6)))
  else:
    if not (opll.mask & OPLL_MASK_BD):
      out[9] = _RO(_calc_slot_car(opll, 6, _calc_slot_mod(opll, 6)))
  _update_noise(opll, 14)
  # CH8
  if not opll.rhythm_mode:
    if not (opll.mask & _OPLL_MASK_CH(7)):
      out[7] = _MO(_calc_slot_car(opll, 7, _calc_slot_mod(opll, 7)))
  else:
    if not (opll.mask & OPLL_MASK_HH):
      out[10] = _RO(_calc_slot_hat(opll))
    if not (opll.mask & OPLL_MASK_SD):
      out[11] = _RO(_calc_slot_snare(opll))
  _update_noise(opll, 2)
  # CH9
  if not opll.rhythm_mode:
    if not (opll.mask & _OPLL_MASK_CH(8)):
      out[8] = _MO(_calc_slot_car(opll, 8, _calc_slot_mod(opll, 8)))
  else:
    if not (opll.mask & OPLL_MASK_TOM):
      out[12] = _RO(_calc_slot_tom(opll))
    if not (opll.mask & OPLL_MASK_CYM):
      out[13] = _RO(_calc_slot_cym(opll))
  _update_noise(opll, 2)

def _mix_output(opll):
  out = 0
  for i in range(14): out += opll.ch_out[i]
  if (opll.conv):  OPLL_RateConv_putData(opll.conv, 0, out)
  else:               opll.mix_out[0] = out

def _mix_output_stereo(opll):
  out = opll.mix_out
  out[0] = out[1] = 0
  for i in range(14):
    if opll.pan[i] & 2:
      out[0] += int(opll.ch_out[i] * opll.pan_fine[i][0])
    if opll.pan[i] & 1:
      out[1] += int(opll.ch_out[i] * opll.pan_fine[i][1])
  if (opll.conv):
    OPLL_RateConv_putData(opll.conv, 0, out[0])
    OPLL_RateConv_putData(opll.conv, 1, out[1])

#***********************************************************
#
#                   External Interfaces
#
#***********************************************************

def OPLL_new(clk, rate):
  return OPLL(clk,rate)

def _reset_rate_conversion_params(opll):
  f_out = opll.rate
  f_inp = opll.clk / 72.0
  opll.out_time = 0
  opll.out_step = f_inp
  opll.inp_step = f_out
  if opll.conv: opll.conv = None
  if math.floor(f_inp) != f_out and math.floor(f_inp + 0.5) != f_out:
    print(f"inp {math.floor(f_inp)} {math.floor(f_inp+0.5)} out {f_out}")
    opll.conv = OPLL_RateConv(f_inp, f_out, 2)
  if opll.conv: OPLL_RateConv_reset(opll.conv)

def OPLL_reset(opll):
  if not opll: return
  opll.adr = 0
  opll.pm_phase = 0
  opll.am_phase = 0
  opll.noise = 0x1
  opll.mask = 0
  opll.rhythm_mode = 0
  opll.slot_key_status = 0
  opll.eg_counter = 0
  _reset_rate_conversion_params(opll)
  opll.slot = [OPLL_SLOT(i) for i in range(18)]
  opll.patch_number = [0] * 9
  for i in range(9):    _set_patch(opll, i, 0)
  opll.reg=[0]*0x40
  opll.chip_type=0
  for i in range(0x40): OPLL_writeReg(opll, i, 0)
  for i in range(15):
    opll.pan[i] = 3
    opll.pan_fine[:] = [1.0,1.0]
  opll.ch_out = [0] * 14

def OPLL_forceRefresh(opll):
  if opll == None: return
  for i in range(9):  _set_patch(opll, i, opll.patch_number[i])
  for i in range(18): _request_update(opll.slot[i], UPDATE_ALL)

def OPLL_setRate(opll, rate):
  opll.rate = rate
  _reset_rate_conversion_params(opll)

def OPLL_setQuality(opll, q): pass

def OPLL_setChipType(opll, type): opll.chip_type = type
def _uint8(d): return int(d) & 0xff

def OPLL_writeReg(opll, reg, data):
  if reg >= 0x40: return
  # mirror registers
  if (0x19 <= reg and reg <= 0x1f) or (0x29 <= reg and reg <= 0x2f) or (0x39 <= reg and reg <= 0x3f):
    reg -= 9
  opll.reg[reg] = _uint8(data)
  match reg:
    case 0x00:
      opll.patch[0].AM = (data >> 7) & 1
      opll.patch[0].PM = (data >> 6) & 1
      opll.patch[0].EG = (data >> 5) & 1
      opll.patch[0].KR = (data >> 4) & 1
      opll.patch[0].ML = (data)&15
      for i in range(9):
        if opll.patch_number[i] == 0:
          _request_update(_MOD(opll, i), UPDATE_RKS | UPDATE_EG)
    case 0x01:
      opll.patch[1].AM = (data >> 7) & 1
      opll.patch[1].PM = (data >> 6) & 1
      opll.patch[1].EG = (data >> 5) & 1
      opll.patch[1].KR = (data >> 4) & 1
      opll.patch[1].ML = (data)&15
      for i in range(9):
        if opll.patch_number[i] == 0:
          _request_update(_CAR(opll, i), UPDATE_RKS | UPDATE_EG)
    case 0x02:
      opll.patch[0].KL = (data >> 6) & 3
      opll.patch[0].TL = (data)&63
      for i in range(9):
        if opll.patch_number[i] == 0:
          _request_update(_MOD(opll, i), UPDATE_TLL)
    case 0x03:
      opll.patch[1].KL = (data >> 6) & 3
      opll.patch[1].WS = (data >> 4) & 1
      opll.patch[0].WS = (data >> 3) & 1
      opll.patch[0].FB = (data)&7
      for i in range(9):
        if (opll.patch_number[i] == 0):
          _request_update(_MOD(opll, i), UPDATE_WS)
          _request_update(_CAR(opll, i), UPDATE_WS | UPDATE_TLL)
    case 0x04:
      opll.patch[0].AR = (data >> 4) & 15
      opll.patch[0].DR = (data)&15
      for i in range(9):
        if opll.patch_number[i] == 0:
          _request_update(_MOD(opll, i), UPDATE_EG)
    case 0x05:
      opll.patch[1].AR = (data >> 4) & 15
      opll.patch[1].DR = (data)&15
      for i in range(9):
        if opll.patch_number[i] == 0:
          _request_update(_CAR(opll, i), UPDATE_EG)
    case 0x06:
      opll.patch[0].SL = (data >> 4) & 15
      opll.patch[0].RR = (data)&15
      for i in range(9):
        if opll.patch_number[i] == 0:
          _request_update(_MOD(opll, i), UPDATE_EG)
    case 0x07:
      opll.patch[1].SL = (data >> 4) & 15
      opll.patch[1].RR = (data)&15
      for i in range(9):
        if opll.patch_number[i] == 0:
          _request_update(_CAR(opll, i), UPDATE_EG)
    case 0x0e:
      if opll.chip_type == 1: pass
      else:
        _update_rhythm_mode(opll)
        _update_key_status(opll)
    case 0x0f:
      opll.test_flag = _uint8(data)
    case 0x10 | 0x11 | 0x12 | 0x13 | 0x14 | 0x15 | 0x16 | 0x17 | 0x18:
      ch = reg - 0x10
      _set_fnumber(opll, ch, data + ((opll.reg[0x20 + ch] & 1) << 8))
    case 0x20 | 0x21 | 0x22 | 0x23 | 0x24 | 0x25 | 0x26 | 0x27 | 0x28:
      ch = reg - 0x20
      _set_fnumber(opll, ch, ((data & 1) << 8) + opll.reg[0x10 + ch])
      _set_block(opll, ch, (data >> 1) & 7)
      _set_sus_flag(opll, ch, (data >> 5) & 1)
      _update_key_status(opll)
    case 0x30 | 0x31 | 0x32 | 0x33 | 0x34 | 0x35 | 0x36 | 0x37 | 0x38:
      if (opll.reg[0x0e] & 32) and (reg >= 0x36):
        match reg:
          case 0x37:
            _set_slot_volume(_MOD(opll, 7), ((data >> 4) & 15) << 2)
          case 0x38:
            _set_slot_volume(_MOD(opll, 8), ((data >> 4) & 15) << 2)
          case _: pass
      else:
        _set_patch(opll, reg - 0x30, (data >> 4) & 15)
      _set_volume(opll, reg - 0x30, (data & 15) << 2)
    case _: pass

def OPLL_writeIO(opll, adr, val):
  if adr & 1:
    OPLL_writeReg(opll, opll.adr, val)
  else:
    opll.adr = val
def OPLL_setPan(opll, ch, pan): opll.pan[ch & 15] = pan

def OPLL_setPanFine(opll, ch, pan):
  opll.pan_fine[ch & 15][0] = pan[0]
  opll.pan_fine[ch & 15][1] = pan[1]

def OPLL_dumpToPatch(dump, patch):
  patch[0].AM = (dump[0] >> 7) & 1
  patch[1].AM = (dump[1] >> 7) & 1
  patch[0].PM = (dump[0] >> 6) & 1
  patch[1].PM = (dump[1] >> 6) & 1
  patch[0].EG = (dump[0] >> 5) & 1
  patch[1].EG = (dump[1] >> 5) & 1
  patch[0].KR = (dump[0] >> 4) & 1
  patch[1].KR = (dump[1] >> 4) & 1
  patch[0].ML = (dump[0]) & 15
  patch[1].ML = (dump[1]) & 15
  patch[0].KL = (dump[2] >> 6) & 3
  patch[1].KL = (dump[3] >> 6) & 3
  patch[0].TL = (dump[2]) & 63
  patch[1].TL = 0
  patch[0].FB = (dump[3]) & 7
  patch[1].FB = 0
  patch[0].WS = (dump[3] >> 3) & 1
  patch[1].WS = (dump[3] >> 4) & 1
  patch[0].AR = (dump[4] >> 4) & 15
  patch[1].AR = (dump[5] >> 4) & 15
  patch[0].DR = (dump[4]) & 15
  patch[1].DR = (dump[5]) & 15
  patch[0].SL = (dump[6] >> 4) & 15
  patch[1].SL = (dump[7] >> 4) & 15
  patch[0].RR = (dump[6]) & 15
  patch[1].RR = (dump[7]) & 15

def OPLL_getDefaultPatch(type, num, patch):
  global _default_inst
  OPLL_dumpToPatch(_default_inst[type][num * 8:], patch)

def OPLL_setPatch(opll, dump):
  patch = [OPLL_PATCH() for _ in range(2)]
  for i in range(19):
    OPLL_dumpToPatch(dump + i * 8, patch)
    opll.patch[i * 2 + 0][:] = patch[0]
    opll.patch[i * 2 + 1][:] = patch[1]

def OPLL_patchToDump(patch, dump):
  dump[0] = _uint8((patch[0].AM << 7) + (patch[0].PM << 6) + (patch[0].EG << 5) + (patch[0].KR << 4) + patch[0].ML)
  dump[1] = _uint8((patch[1].AM << 7) + (patch[1].PM << 6) + (patch[1].EG << 5) + (patch[1].KR << 4) + patch[1].ML)
  dump[2] = _uint8((patch[0].KL << 6) + patch[0].TL)
  dump[3] = _uint8((patch[1].KL << 6) + (patch[1].WS << 4) + (patch[0].WS << 3) + patch[0].FB)
  dump[4] = _uint8((patch[0].AR << 4) + patch[0].DR)
  dump[5] = _uint8((patch[1].AR << 4) + patch[1].DR)
  dump[6] = _uint8((patch[0].SL << 4) + patch[0].RR)
  dump[7] = _uint8((patch[1].SL << 4) + patch[1].RR)

def OPLL_copyPatch(opll, num, patch):
  opll.patch[num] = patch.copy()

def OPLL_resetPatch(opll, type):
  for i in range(19):
    opll.patch[i] = _default_patch[type % OPLL_TONE_NUM][i>>1][i & 1]

def OPLL_calc(opll):
  while opll.out_step > opll.out_time:
    opll.out_time += opll.inp_step
    _update_output(opll)
    _mix_output(opll)
  opll.out_time -= opll.out_step
  if opll.conv: opll.mix_out[0] = OPLL_RateConv_getData(opll.conv, 0)
  return opll.mix_out[0]

def OPLL_calcStereo(opll, out):
  while opll.out_step > opll.out_time:
    opll.out_time += opll.inp_step
    _update_output(opll)
    _mix_output_stereo(opll)
  opll.out_time -= opll.out_step
  if opll.conv:
    out[0] = OPLL_RateConv_getData(opll.conv, 0)
    out[1] = OPLL_RateConv_getData(opll.conv, 1)
  else:
    out[0] = opll.mix_out[0]
    out[1] = opll.mix_out[1]

def OPLL_setMask(opll, mask):
  if not opll: return 0
  ret = opll.mask
  opll.mask = mask
  return ret

def OPLL_toggleMask(opll, mask):
  if not opll: return 0
  ret = opll.mask
  opll.mask ^= mask
  return ret
