#!/usr/bin/env bash
cutechess-cli \
    -engine "conf=Stockfish 1320" \
    -engine "conf=Stockfish 1500" \
    -engine "conf=NB Latest" \
    -engine "conf=NB v4_transpos" \
    -engine "conf=NB v5_good_transpos" \
    -engine "conf=NB v6_transpos_order" \
    -engine "conf=NB v7_minimized_986" \
    -each st=0.05 timemargin=200 \
    -games 250 -concurrency 32 \
    -ratinginterval 10
