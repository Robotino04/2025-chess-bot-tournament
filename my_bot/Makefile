.PHONY: all clean measure

all: measure example_bot

measure: example_bot.c
	java -jar ~/Downloads/toknt.jar example_bot.c

example_bot: example_bot.c
	gcc -std=c23 -O3 -o example_bot example_bot.c -L. -lchess -lm


clean:
	rm -f example_bot
