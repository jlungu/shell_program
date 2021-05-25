CC=gcc
CFLAGS= -Wall -Werror
TISH=tish

%.o: %.c
	$(CC) $(CFLAGS) -c $^

tish: tish.o
	$(CC) $(CFLAGS) $< -o $@

tests: tish
	sh test01.sh && sh test02.sh && sh test03.sh && sh test04.sh && sh test05.sh && sh test06.sh && sh test07.sh && sh test08.sh && sh test09.sh && sh test10.sh && sh test11.sh && sh test12.sh && sh test13.sh
.PHONY: clean
clean:
	rm $(TISH) && rm *.o 
