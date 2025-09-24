.PHONY: all clean

all: example_bot

example_bot: example_bot.c
	gcc -std=c23 -O3 -o example_bot example_bot.c -L. -lchess -lm


clean:
	rm -f example_bot
