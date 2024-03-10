build:
	@echo $(OPTION)
	@python mmlc.py > bgm1.h
	@sdcc -mz80 $(OPTION) oplldrv.c --opt-code-speed --no-std-crt0 -o a.ihx
	@../../ihx2bin a.ihx -o a.bin
	@../../6448 a.bin > result
	@open opll.wav
	@make clean
a:
	make build -e "OPTION=-D OPT=1"
b:
	make build -e "OPTION=-D OPT=1 -D OPT2=1"
c:
	make build -e "OPTION=-D OPT=1 -D OPT2=1 -D OPT3=1"
clean:
	@rm -rf *.ihx *.lk *.noi *.lst *.map *.sym *.rel *.bin a.asm
