t: 
	make build -e "SRC=spehari"
build:
	@echo $(OPTION)
	@python mmlc.py res/$(SRC).mml bgm1 > bgm1.h
	@sdcc -mz80 $(OPTION) oplldrv.c --opt-code-speed -c
	@sdcc -mz80 $(OPTION) main.c oplldrv.rel --opt-code-speed --no-std-crt0 -o a.ihx
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
clean:
	@rm -rf *.ihx *.lk *.noi *.lst *.map *.sym *.rel *.bin a.asm
