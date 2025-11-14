CFLAGS = -std=c23 -O3 -L. -g -Wall -Wextra
CPPFLAGS = -O3 -L. -g -Wall -Wextra -ffast-math -march=native -fopenmp=libomp
LDFLAGS = -lchess -lm -g

CC = gcc
CXX = clang++

SRC_ENGINE := engine/
BUILD_DIR := build/
RESOURCES := resources/

BUILD_OUT := ${BUILD_DIR}/bin/
TOOL_OUT := ${BUILD_DIR}/tools/

TOKNT := ${TOOL_OUT}/toknt.jar

.PHONY: all clean clean-all measure measure_minimized measure_formatted tournament test

all: measure ${BUILD_OUT}/thera_mini ${BUILD_OUT}/thera_mini_minimized measure_minimized measure_formatted ${BUILD_OUT}/train_nn

measure: ${SRC_ENGINE}/thera_mini.c ${TOKNT}
	java -jar ${TOKNT} $<

measure_minimized: ${BUILD_OUT}/thera_mini_minimized.c ${TOKNT}
	java -jar ${TOKNT} $<

measure_formatted: ${BUILD_OUT}/thera_mini_formatted.c ${TOKNT}
	java -jar ${TOKNT} $<

${BUILD_OUT}/thera_mini_%: ${BUILD_OUT}/thera_mini_%.c
	mkdir -p ${BUILD_OUT}
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

${BUILD_OUT}/thera_mini: ${SRC_ENGINE}/thera_mini.c
	mkdir -p ${BUILD_OUT}
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

${BUILD_OUT}/train_nn: ${SRC_ENGINE}/train_nn.cpp
	mkdir -p ${BUILD_OUT}
	$(CXX) $(CPPFLAGS) $(LDFLAGS) -o $@ $<

${BUILD_OUT}/thera_mini_clean_pcpp.c: ${SRC_ENGINE}/thera_mini.c
	mkdir -p ${BUILD_OUT}
	pcpp --passthru-unfound-includes --passthru-includes ".*" --line-directive "" -D MINIMIZE $< -o $@

${BUILD_OUT}/thera_mini_clean_clang_tidy.c: ${BUILD_OUT}/thera_mini_clean_pcpp.c ${RESOURCES}/minimal.clang-tidy-fixes
	mkdir -p ${BUILD_OUT}
	cp $< $@
	clang-tidy $@ --fix --quiet $(cat ${RESOURCES}/minimal.clang-tidy-fixes) -- $(CFLAGS) -I../src/c/

${BUILD_OUT}/thera_mini_clean_uncrustify.c: ${BUILD_OUT}/thera_mini_clean_clang_tidy.c ${RESOURCES}/uncrustify.ini
	mkdir -p ${BUILD_OUT}
	uncrustify -lc -c ${RESOURCES}/uncrustify.ini < $< > $@

${BUILD_OUT}/thera_mini_clean_clang_format.c: ${BUILD_OUT}/thera_mini_clean_uncrustify.c ${RESOURCES}/minimal.clang-format
	mkdir -p ${BUILD_OUT}
	clang-format --style=file:${RESOURCES}/minimal.clang-format < $< > $@

${BUILD_OUT}/thera_mini_clean.c: ${BUILD_OUT}/thera_mini_clean_clang_format.c
	mkdir -p ${BUILD_OUT}
	cp $< $@

${BUILD_OUT}/thera_mini_minimized.c: ${BUILD_OUT}/thera_mini_clean.c minimizer/src/main.rs ${TOKNT}
	mkdir -p ${BUILD_OUT}
	java -jar ${TOKNT} $<
	cargo run --release --manifest-path=./minimizer/Cargo.toml -- $< ${BUILD_OUT}

${BUILD_OUT}/thera_mini_formatted.c: ${BUILD_OUT}/thera_mini_minimized.c
	mkdir -p ${BUILD_OUT}
	# the minimizer generates both
	touch $@

${TOKNT}:
	mkdir -p $(dir $@)
	wget -q --show-progress https://github.com/chayleaf/toknt/releases/download/latest/toknt.jar -O $@

tournament: ${BUILD_OUT}/thera_mini ${BUILD_OUT}/thera_mini_minimized ${RESOURCES}/tournament.sh ${RESOURCES}/track_progress.sh
	./${RESOURCES}/track_progress.sh ./${RESOURCES}/tournament.sh

ENGINE_VERSION := thera_mini_formatted

COMMON_TEST_ARGS := \
	-engine "name=NB Latest" cmd=${BUILD_OUT}/$(ENGINE_VERSION) \
	-engine "name=NB v24_numbers " cmd=./backups/v24_numbers \
	-games 999999 -concurrency 16 \
	-recover \
	-repeat 2 \
	-each tc=1 timemargin=200 proto=uci \
	-ratinginterval 10 \
	-openings file="${TOOL_OUT}/UHO_Lichess_4852_v1.epd" format=epd order=random policy=default
    #-openings file="${RESOURCES}/book-ply6-unifen_Q.txt.dont_lsp" format=epd order=random plies=6 policy=default

test: ${BUILD_OUT}/$(ENGINE_VERSION) ${TOOL_OUT}/UHO_Lichess_4852_v1.epd ${RESOURCES}/book-ply6-unifen_Q.txt.dont_lsp
	cutechess-cli $(COMMON_TEST_ARGS) -sprt elo0=0 elo1=10 alpha=0.05 beta=0.05 | tee /tmp/match.log

untest: ${BUILD_OUT}/$(ENGINE_VERSION) ${TOOL_OUT}/UHO_Lichess_4852_v1.epd ${RESOURCES}/book-ply6-unifen_Q.txt.dont_lsp
	cutechess-cli $(COMMON_TEST_ARGS) -sprt elo0=-10 elo1=0 alpha=0.05 beta=0.05 | tee /tmp/match.log

monitor:
	./${RESOURCES}/monitor.sh

${TOOL_OUT}/UHO_Lichess_4852_v1.epd: ${RESOURCES}/UHO_Lichess_4852_v1.epd.zip
	mkdir -p ${TOOL_OUT}
	unzip $< -d $(dir $@)
	touch $@

clean-all:
	rm -rf ${BUILD_DIR}
clean:
	rm -rf ${BUILD_OUT}
