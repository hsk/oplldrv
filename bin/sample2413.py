import emu2413

MSX_CLK = 3579545 # Standard clock = MSX clock
SAMPLERATE = 44100
DATALENGTH = SAMPLERATE * 8

def write16(buf, data):
  buf.extend([data & 0xff, (data & 0xff00) >> 8])
def write32(buf, data):
  buf.extend([data & 0xff, (data & 0xff00) >> 8,
              (data & 0xff0000) >> 16, (data & 0xff000000) >> 24])
def chunk(buf, id):
  buf.extend([ord(i) for i in id])

def write_wav(filename, out):
  # Create WAVE header
  header=bytearray()
  chunk(header, "RIFF")
  write32(header, DATALENGTH * 2 + 36)
  chunk(header, "WAVE")
  chunk(header, "fmt ")
  write32(header, 16)
  write16(header, 1);               # WAVE_FORMAT_PCM
  write16(header, 1);               # channel 1=mono,2=stereo
  write32(header, SAMPLERATE);     # samplesPerSec
  write32(header, 2 * SAMPLERATE); # bytesPerSec
  write16(header, 2);               # blockSize
  write16(header, 16);              # bitsPerSample
  chunk(header, "data")
  write32(header, 2 * DATALENGTH)
  write16(header, 0)

  fp = open(filename, "wb")
  if fp == None: return 1
  fp.write(header)
  fp.write(out)
  fp.close()
  print("Wrote : {}".format(filename))
  
def main():

  opll = emu2413.OPLL_new(MSX_CLK, SAMPLERATE)
  emu2413.OPLL_reset(opll)
  emu2413.OPLL_writeReg(opll, 0x30, 0x30); # select PIANO Voice to ch1.
  emu2413.OPLL_writeReg(opll, 0x10, 0x80); # set F-Number(L).
  emu2413.OPLL_writeReg(opll, 0x20, 0x15); # set BLK & F-Number(H) and keyon.

  out = bytearray()
  for _ in range(DATALENGTH):
    write16(out, emu2413.OPLL_calc(opll))

  write_wav("temp2.wav", out)

main()
