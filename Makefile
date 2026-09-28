# LOOPS : 無限ループの曲を何周鳴らすか
# FRAMES: 演奏するフレーム数を固定する (例: make 01 FRAMES=3600 で 60 秒。CPU使用率の計測用)
# OPEN  : 出来上がった WAV を開くコマンド (OPEN=true で開かない)
LOOPS = 2
OPEN = open
ifdef FRAMES
FRAMES_OPT = -D FRAMES=$(FRAMES)
endif
EMU_SRC = bin/z80emu6448.c bin/emu2149.c bin/emu76489.c bin/emu2413.c
# RAM (変数) を置く番地。プログラムと曲のデータは 0x200 から置くので、それより下に収まらないといけない。
# エミュレータのメモリは 64KB で、スタックは 0xFFFC から下に伸びる
DATA_LOC = 0xE000

# MML に PSG (1〜3) のチャンネルの行があれば -D PSG=1 を付ける (無ければ今と同じコード)
PSG_OPT = $(shell grep -qE '^[1-9A-Ha-h]*[1-3][1-9A-Ha-h]*[[:space:]]' res/$(SRC).mml && echo -D PSG=1)
# ビルドの結果 (result・opll.wav など) を置くところ: build/曲名/版。版は OPTION から作る (C 版は c、
# -D OPT=1 -D OPT2=1 -D OPT3=1 は OPT_OPT2_OPT3)。FRAMES を付けたら _f3600 のように足す。
# 曲や版ごとに分かれるので、make -j で並べて動かしたり、作業中に別の曲をビルドしたりできる
ASM = -D OPT=1 -D OPT2=1 -D OPT3=1
space := $(subst ,, )
VARIANT = $(or $(subst $(space),_,$(strip $(subst -D ,,$(subst =1,,$(OPTION))))),c)$(if $(FRAMES),_f$(FRAMES))
B = build/$(SRC)/$(VARIANT)

t: 
	$(MAKE) build -e "SRC=spehari"
# build/ というディレクトリがあっても、ターゲットの build は毎回動かす
.PHONY: build clean distclean mgscmp vgm vgz mp4
build: 6448 ihx2bin
	@rm -rf $(B) && mkdir -p $(B)
	@echo $(OPTION) $(PSG_OPT) "→ $(B)"
	@python mmlc.py res/$(SRC).mml bgm1 $(LOOPS) > $(B)/bgm1.h
	@cp $(B)/bgm1.h data/$(SRC).h.$$$$ && mv data/$(SRC).h.$$$$ data/$(SRC).h
	@sdcc -mz80 $(OPTION) $(PSG_OPT) oplldrv.c --opt-code-speed -c -o $(B)/oplldrv.rel
	@sdcc -mz80 $(OPTION) $(FRAMES_OPT) -D SRC=\"$(B)/bgm1.h\" main.c --opt-code-speed -c -o $(B)/main.rel
	@sdcc -mz80 $(B)/main.rel $(B)/oplldrv.rel --no-std-crt0 --data-loc $(DATA_LOC) -o $(B)/a.ihx
	@python3 -c "import re,sys; m=open('$(B)/a.map').read(); g=lambda k: int(re.search(r'([0-9A-F]+)\s+'+k+r'\s',m).group(1),16); e=g('s__CODE')+g('l__CODE'); e>$(DATA_LOC) and sys.exit('error: プログラムと曲のデータ (0x%04X まで) が RAM ($(DATA_LOC) から) に重なる' % e)"
	@./ihx2bin $(B)/a.ihx -o $(B)/a.bin
	@cd $(B) && $(CURDIR)/6448 a.bin > result
	@cd $(B) && rm -f *.ihx *.lk *.noi *.lst *.map *.sym *.rel *.asm
	@$(OPEN) $(B)/opll.wav
a:
	$(MAKE) build -e "OPTION=-D OPT=1" -e "SRC=spehari"
b:
	$(MAKE) build -e "OPTION=-D OPT=1 -D OPT2=1" -e "SRC=spehari"
c:
	$(MAKE) build -e "OPTION=-D OPT=1 -D OPT2=1 -D OPT3=1" -e "SRC=spehari"
d:
	$(MAKE) build -e "OPTION=-D OPT=1 -D OPT2=1 -D OPT3=1" -e "SRC=spehari2"
t1:
	$(MAKE) build -e "SRC=break"
t1c:
	$(MAKE) build -e "SRC=break" -e "OPTION=-D OPT=1 -D OPT2=1 -D OPT3=1"
t2:
	$(MAKE) build -e "SRC=sound"
t2c:
	$(MAKE) build -e "SRC=sound" -e "OPTION=-D OPT=1 -D OPT2=1 -D OPT3=1"
t3:
	$(MAKE) build -e "SRC=drum"
t3c:
	$(MAKE) build -e "SRC=drum" -e "OPTION=-D OPT=1 -D OPT2=1 -D OPT3=1"

01 02 03 04 05 06 07 08 09 10 11 13 14 15 16 17 18 19 20 21 22 23 24 26 27 28 29 30 40 41 42 55 58:
	$(MAKE) build -e "OPTION=-D OPT=1 -D OPT2=1 -D OPT3=1" -e "SRC=ys2_$@"

ys2_%:
	$(MAKE) build -e "SRC=ys2_$*"

# MGSDRV との比較: make mgscmp-01 (1 曲)、make mgscmp (全曲。結果は mgscmp/*.txt)
# 同じ MML を oplldrv と MGSDRV (bin/msxplay) で鳴らし、OPLL への書き込みを bin/mgscmp.py で比べる
# 曲ごとに build/ に分かれるので、make -j8 mgscmp で並べて動かせる
YS2 = 01 02 03 04 05 06 07 08 09 10 11 13 14 15 16 17 18 19 20 21 22 23 24 26 27 28 29 30 40 41 42 50 51 52 53 54 55 56 57 58 59
mgscmp: $(addprefix mgscmp-,$(YS2))
	@cat mgscmp/*.txt | grep -E '^==|合計'
mgscmp-%: bin/msxplay/node_modules 6448 ihx2bin
	@mkdir -p mgscmp
	@$(MAKE) build OPTION="$(ASM)" SRC=ys2_$* FRAMES= OPEN=true 2>/dev/null >/dev/null
	@cp build/ys2_$*/OPT_OPT2_OPT3/result mgscmp/ys2_$*.opll.log
	@node bin/msxplay/mgs2log.mjs res/ys2_$*.mml --frames $$(grep -c '^wait' mgscmp/ys2_$*.opll.log) > mgscmp/ys2_$*.mgs.log
	@python3 bin/mgscmp.py mgscmp/ys2_$*.opll.log mgscmp/ys2_$*.mgs.log $(V) > mgscmp/ys2_$*.txt
	@cat mgscmp/ys2_$*.txt
# MGSDRV で鳴らして聞く: make m01 (FRAMES を付けなければ 60 秒。結果は build/ys2_01/mgs.wav)
$(addprefix m,$(YS2)): m%: bin/msxplay/node_modules
	@mkdir -p build/ys2_$*
	@node bin/msxplay/mgs2log.mjs res/ys2_$*.mml --frames $(or $(FRAMES),3600) --wav build/ys2_$*/mgs.wav > /dev/null
	@$(OPEN) build/ys2_$*/mgs.wav
bin/msxplay/node_modules: bin/msxplay/package.json
	cd bin/msxplay && npm install
	@touch $@

# VGM 出力: make vgm-01 (1 曲)、make vgm (全曲)。vgz にするなら make vgz-01、make vgz。結果は vgm/ に出る
# エミュレータの OPLL への書き込みログ (result) を bin/vgm.py で VGM に変える
vgm: $(addprefix vgm-,$(YS2))
vgz: $(addprefix vgz-,$(YS2))
vgm-% vgz-%: 6448 ihx2bin
	@mkdir -p vgm
	@$(MAKE) build OPTION="$(ASM)" SRC=ys2_$* FRAMES= OPEN=true 2>/dev/null >/dev/null
	@python3 bin/vgm.py build/ys2_$*/OPT_OPT2_OPT3/result vgm/ys2_$*.$(firstword $(subst -, ,$@)) res/ys2_$*.mml

# 動画: make mp4 SRC=ys2_01 (先に make 01 でアセンブラ版をビルドしておく)
mp4:
	python title.py "YM2413 DEMO"
	ffmpeg -y -loop 1 -i title.png -i build/$(SRC)/OPT_OPT2_OPT3/opll.wav -shortest -vcodec libx264 -pix_fmt yuv420p -acodec aac opll.mp4
	open opll.mp4
6448: $(EMU_SRC) bin/z80emu.h bin/emu2149.h bin/emu76489.h bin/emu2413.h
	gcc $(EMU_SRC) -o 6448
ihx2bin: bin/ihx2bin.cpp
	gcc bin/ihx2bin.cpp -o ihx2bin
clean:
	@rm -rf *.ihx *.lk *.noi *.lst *.map *.sym *.rel *.bin a.asm oplldrv.asm
distclean: clean
	rm -rf 6448 ihx2bin build result opll.wav mgs.wav psg.wav sng.wav opll.mp4 mgscmp bin/msxplay/node_modules
