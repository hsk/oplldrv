# LOOPS : 無限ループの曲を何周鳴らすか
# FRAMES: 演奏するフレーム数を固定する (例: make 01 FRAMES=3600 で 60 秒。CPU使用率の計測用)
# OPEN  : 出来上がった WAV を開くコマンド (OPEN=true で開かない)
LOOPS = 2
OPEN = open
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
	@$(OPEN) opll.wav
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

# MGSDRV との比較: make mgscmp-01 (1 曲)、make mgscmp (全曲。結果は mgscmp/*.txt)
# 同じ MML を oplldrv と MGSDRV (bin/msxplay) で鳴らし、OPLL への書き込みを bin/mgscmp.py で比べる
# result や a.bin を共有するので並列に動かさない
.NOTPARALLEL:
YS2 = 01 02 03 04 05 06 07 08 09 10 11 13 14 15 16 17 18 19 20 21 22 23 24 26 27 28 29 30 40 41 42
mgscmp: $(addprefix mgscmp-,$(YS2))
	@cat mgscmp/*.txt | grep -E '^==|合計'
mgscmp-%: bin/msxplay/node_modules
	@mkdir -p mgscmp
	@make build -e "OPTION=-D OPT=1 -D OPT2=1 -D OPT3=1" -e "SRC=ys2_$*" OPEN=true 2>/dev/null >/dev/null
	@cp result mgscmp/ys2_$*.opll.log
	@node bin/msxplay/mgs2log.mjs res/ys2_$*.mml --frames $$(grep -c '^wait' result) > mgscmp/ys2_$*.mgs.log
	@python3 bin/mgscmp.py mgscmp/ys2_$*.opll.log mgscmp/ys2_$*.mgs.log $(V) | tee mgscmp/ys2_$*.txt
bin/msxplay/node_modules: bin/msxplay/package.json
	cd bin/msxplay && npm install
	@touch $@

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
	rm -rf 6448 ihx2bin result opll.wav psg.wav sng.wav opll.mp4 mgscmp bin/msxplay/node_modules
