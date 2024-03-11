t: 
	make build -e "SRC=spehari"
build:
	@echo $(OPTION)
	@python mmlc.py res/$(SRC).mml bgm1 > bgm1.h
	@sdcc -mz80 $(OPTION) oplldrv.c --opt-code-speed --no-std-crt0 -o a.ihx
	@../../ihx2bin a.ihx -o a.bin
	@../../6448 a.bin > result
	@open opll.wav
	@make clean
a:
	make build -e "OPTION=-D OPT=1" -e "SRC=spehari"
b:
	make build -e "OPTION=-D OPT=1 -D OPT2=1" -e "SRC=spehari"
c:
	make build -e "OPTION=-D OPT=1 -D OPT2=1 -D OPT3=1" -e "SRC=spehari"
t1:
	make build -e "SRC=break"
t1c:
	make build -e "SRC=break" -e "OPTION=-D OPT=1 -D OPT2=1 -D OPT3=1"
t2:
	make build -e "SRC=sound"
t2c:
	make build -e "SRC=sound" -e "OPTION=-D OPT=1 -D OPT2=1 -D OPT3=1"


01 02 03 04 05 06 07 08 09 10 11 13 14 15 16 17 18 19 20 21 22 23 24 26 27 28 29 30:
	make build -e "OPTION=-D OPT=1 -D OPT2=1 -D OPT3=1" -e "SRC=ys2_$@"

ys2_%:
	make build -e "SRC=ys2_$*"

clean:
	@rm -rf *.ihx *.lk *.noi *.lst *.map *.sym *.rel *.bin a.asm
