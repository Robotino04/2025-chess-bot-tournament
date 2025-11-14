CFLAGS= -std=c23 -O3 -L. -g -Wall -Wextra
CPPFLAGS= -O3 -L. -g -Wall -Wextra -ffast-math -march=native -fopenmp=libomp
LDFLAGS= -lchess -lm -g

CC= gcc
CXX= clang++

.PHONY: all clean measure measure_minimized measure_formatted tournament test

all: measure thera_mini thera_mini_minimized measure_minimized measure_formatted train_nn

measure: thera_mini.c
	java -jar ~/Downloads/toknt.jar thera_mini.c

measure_minimized: thera_mini_minimized.c
	java -jar ~/Downloads/toknt.jar thera_mini_minimized.c

measure_formatted: thera_mini_formatted.c
	java -jar ~/Downloads/toknt.jar thera_mini_formatted.c

thera_mini: thera_mini.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o thera_mini thera_mini.c

thera_mini_minimized: thera_mini_minimized.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o thera_mini_minimized thera_mini_minimized.c

thera_mini_formatted: thera_mini_formatted.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o thera_mini_formatted thera_mini_formatted.c

train_nn: train_nn.cpp
	$(CXX) $(CPPFLAGS) $(LDFLAGS) -o train_nn train_nn.cpp

thera_mini_clean.c: thera_mini.c minimize.py uncrustify.ini minimal.clang-format minimal.clang-tidy-fixes
	pcpp --passthru-unfound-includes --passthru-includes ".*" --line-directive "" -D MINIMIZE thera_mini.c -o thera_mini_clean_pcpp.c
	cp thera_mini_clean_pcpp.c thera_mini_clean_clang_tidy.c 
	clang-tidy thera_mini_clean_clang_tidy.c --fix --quiet $(cat minimal.clang-tidy-fixes) -- $(CFLAGS) -I../src/c/
	uncrustify -lc -c uncrustify.ini < thera_mini_clean_clang_tidy.c > thera_mini_clean_uncrustify.c
	clang-format --style=file:minimal.clang-format < thera_mini_clean_uncrustify.c > thera_mini_clean_clang_format.c

	cp thera_mini_clean_clang_format.c thera_mini_clean.c

	#python3 minimize.py < thera_mini.c > thera_mini_clean.c

thera_mini_minimized.c: thera_mini_clean.c minimizer/src/main.rs
	java -jar ~/Downloads/toknt.jar thera_mini_clean.c
	cd minimizer && cargo run --release

thera_mini_formatted.c: thera_mini_minimized.c
	# the minimizer generates both
	touch thera_mini_formatted.c

tournament: thera_mini thera_mini_minimized tournament.sh
	./track_progress.sh ./tournament.sh

ENGINE_VERSION := thera_mini_formatted

COMMON_TEST_ARGS := \
	-engine "name=NB Latest" cmd=./$(ENGINE_VERSION) \
	-engine "name=NB v24_numbers " cmd=./backups/v24_numbers \
	-games 999999 -concurrency 16 \
	-recover \
	-repeat 2 \
	-each tc=1 timemargin=200 proto=uci \
	-ratinginterval 10 \
	-openings file="UHO_Lichess_4852_v1.epd" format=epd order=random policy=default 
    #-openings file="book-ply6-unifen_Q.txt.dont_lsp" format=epd order=random plies=6 policy=default 


test: $(ENGINE_VERSION) UHO_Lichess_4852_v1.epd book-ply6-unifen_Q.txt.dont_lsp
	cutechess-cli $(COMMON_TEST_ARGS) -sprt elo0=0 elo1=10 alpha=0.05 beta=0.05 | tee /tmp/match.log

untest: $(ENGINE_VERSION) UHO_Lichess_4852_v1.epd book-ply6-unifen_Q.txt.dont_lsp
	cutechess-cli $(COMMON_TEST_ARGS) -sprt elo0=-10 elo1=0 alpha=0.05 beta=0.05 | tee /tmp/match.log

monitor:
	./monitor.sh

UHO_Lichess_4852_v1.epd: UHO_Lichess_4852_v1.epd.zip
	unzip UHO_Lichess_4852_v1.epd.zip
	touch $@

OUTPUTS:= thera_mini \
		thera_mini_minimized \
		thera_mini_minimized.c \
		thera_mini_formatted \
		thera_mini_formatted.c \
		thera_mini_clean.c \
		thera_mini_clean_pcpp.c \
		thera_mini_clean_uncrustify.c \
		thera_mini_clean_clang_tidy.c \
		thera_mini_clean_clang_format.c \
		train_nn 

clean:
	rm -f $(OUTPUTS)
