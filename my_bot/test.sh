#!/usr/bin/env bash
cutechess-cli \
    -engine "conf=Stockfish 1320" \
    -engine "conf=Stockfish 1500" \
    -engine "name=NB Latest" cmd=./example_bot \
    -engine "name=NB Latest Minimized" cmd=./example_bot_minimized \
    -engine "name=NB v4_transpos" cmd=./backups/v4_transpos \
    -engine "name=NB v5_good_transpos" cmd=./backups/v5_good_transpos \
    -engine "name=NB v6_transpos_order" cmd=./backups/v6_transpos_order \
    -engine "name=NB v7_minimized_986" cmd=./backups/v7_minimized_986 \
    -engine "name=NB v8_really_small" cmd=./backups/v8_really_small \
    -openings file="book-ply6-unifen_Q.txt" format=epd order=random plies=6 policy=default \
    -each st=0.05 timemargin=200 proto=uci restart=on \
    -games 350 -concurrency 32 \
    -ratinginterval 10
