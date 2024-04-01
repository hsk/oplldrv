all:
	gcc z80emu6448.c emu2149.c emu76489.c emu2413.c -o ../6448
	gcc ihx2bin.cpp -o ../ihx2bin
76489:
	gcc sample76489.c emu76489.c -o sample76489
	./sample76489
2149:
	gcc sample2149.c emu2149.c -o sample2149
	./sample2149
clean:
	rm -rf sample2149 sample76489 *.wav  