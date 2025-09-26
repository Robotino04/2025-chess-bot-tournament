.PHONY: all clean measure measure_minimized

all: measure example_bot example_bot_minimized measure_minimized

measure: example_bot.c
	java -jar ~/Downloads/toknt.jar example_bot.c

measure_minimized: example_bot_minimized.c
	java -jar ~/Downloads/toknt.jar example_bot_minimized.c

example_bot: example_bot.c
	gcc -std=c23 -O3 -o example_bot example_bot.c -L. -lchess -lm

example_bot_minimized: example_bot_minimized.c
	gcc -std=c23 -O3 -o example_bot_minimized example_bot_minimized.c -L. -lchess -lm

example_bot_minimized.c: example_bot.c minimize.py
	python3 minimize.py

clean:
	rm -f example_bot example_bot_minimized
