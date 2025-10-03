CFLAGS= -std=c23 -O3 -L.
LDFLAGS= -lchess -lm

.PHONY: all clean measure measure_minimized

all: measure example_bot example_bot_minimized measure_minimized

measure: example_bot.c
	java -jar ~/Downloads/toknt.jar example_bot.c

measure_minimized: example_bot_minimized.c
	java -jar ~/Downloads/toknt.jar example_bot_minimized.c

example_bot: example_bot.c
	gcc $(CFLAGS) $(LDFLAGS) -o example_bot example_bot.c

example_bot_minimized: example_bot_minimized.c
	gcc $(CFLAGS) $(LDFLAGS) -o example_bot_minimized example_bot_minimized.c

example_bot_clean.c: example_bot.c minimize.py uncrustify.ini minimal.clang-format minimal.clang-tidy-fixes
	pcpp --passthru-unfound-includes --passthru-includes ".*" --line-directive "" -D MINIMIZE example_bot.c -o example_bot_clean_pcpp.c
	cp example_bot_clean_pcpp.c example_bot_clean_clang_tidy.c 
	clang-tidy example_bot_clean_clang_tidy.c --fix --quiet $(cat minimal.clang-tidy-fixes) -- $(CFLAGS) -I../src/c/
	uncrustify -lc -c uncrustify.ini < example_bot_clean_clang_tidy.c > example_bot_clean_uncrustify.c
	clang-format --style=file:minimal.clang-format < example_bot_clean_uncrustify.c > example_bot_clean_clang_format.c

	cp example_bot_clean_clang_format.c example_bot_clean.c

	#python3 minimize.py < example_bot.c > example_bot_clean.c

example_bot_minimized.c: example_bot_clean.c minimizer/src/main.rs
	java -jar ~/Downloads/toknt.jar example_bot_clean.c
	cd minimizer && cargo run --release

clean:
	rm -f example_bot example_bot_minimized example_bot_clean.c example_bot_minimized.c example_bot_clean_pcpp.c example_bot_clean_clang_tidy.c example_bot_clean_clang_format.c 
