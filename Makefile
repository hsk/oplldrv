# LOOPS : 無限ループの曲を何周鳴らすか
# FRAMES: 演奏するフレーム数を固定する (例: make 01 FRAMES=3600 で 60 秒。CPU使用率の計測用)
LOOPS = 2
ifdef FRAMES
FRAMES_OPT = -D FRAMES=$(FRAMES)
endif
EMU_SRC = bin/z80emu6448.c bin/emu2149.c bin/emu76489.c bin/emu2413.c

t: 
	make build -e "SRC=spehari"
build: 6448 ihx2bin
	@echo $(OPTION)
	@python mmlc.py res/$(SRC).mml bgm1 $(LOOPS) > data/$(SRC).h
	@sdcc -mz80 $(OPTION) oplldrv.c --opt-code-speed -c
	@sdcc -mz80 $(OPTION) $(FRAMES_OPT) -D SRC=\"data/$(SRC).h\" main.c oplldrv.rel --opt-code-speed --no-std-crt0 -o a.ihx
	@./ihx2bin a.ihx -o a.bin
	@./6448 a.bin > result
	@open opll.wav
	@make clean
a:
	make build -e "OPTION=-D OPT=1" -e "SRC=spehari"
b:
	make build -e "OPTION=-D OPT=1 -D OPT2=1" -e "SRC=spehari"
c:
	make build -e "OPTION=-D OPT=1 -D OPT2=1 -D OPT3=1" -e "SRC=spehari"
d:
	make build -e "OPTION=-D OPT=1 -D OPT2=1 -D OPT3=1" -e "SRC=spehari2"
t1:
	make build -e "SRC=break"
t1c:
	make build -e "SRC=break" -e "OPTION=-D OPT=1 -D OPT2=1 -D OPT3=1"
t2:
	make build -e "SRC=sound"
t2c:
	make build -e "SRC=sound" -e "OPTION=-D OPT=1 -D OPT2=1 -D OPT3=1"
t3:
	make build -e "SRC=drum"
t3c:
	make build -e "SRC=drum" -e "OPTION=-D OPT=1 -D OPT2=1 -D OPT3=1"

01 02 03 04 05 06 07 08 09 10 11 13 14 15 16 17 18 19 20 21 22 23 24 26 27 28 29 30 40 41 42:
	make build -e "OPTION=-D OPT=1 -D OPT2=1 -D OPT3=1" -e "SRC=ys2_$@"

ys2_%:
	make build -e "SRC=ys2_$*"

mp4:
	python title.py "YM2413 DEMO"
	ffmpeg -y -loop 1 -i title.png -i opll.wav -shortest -vcodec libx264 -pix_fmt yuv420p -acodec aac opll.mp4
	open opll.mp4
6448: $(EMU_SRC) bin/z80emu.h bin/emu2149.h bin/emu76489.h bin/emu2413.h
	gcc $(EMU_SRC) -o 6448
ihx2bin: bin/ihx2bin.cpp
	gcc bin/ihx2bin.cpp -o ihx2bin
clean:
	@rm -rf *.ihx *.lk *.noi *.lst *.map *.sym *.rel *.bin a.asm oplldrv.asm
distclean: clean
	rm -f 6448 ihx2bin result opll.wav psg.wav sng.wav opll.mp4
