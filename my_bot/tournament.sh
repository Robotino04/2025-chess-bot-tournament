#!/usr/bin/env bash
cutechess-cli \
    -engine "conf=Stockfish 1320" \
    -engine "conf=Stockfish 1500" \
    -engine "name=NB Latest" cmd=./example_bot \
    -engine "name=NB Latest Minimized" cmd=./example_bot_minimized \
    -engine "name=NB v12_root_ordering" cmd=./backups/v12_root_ordering \
    -engine "name=NB v9_negascout_tc" cmd=./backups/v9_negascout_tc \
    -openings file="book-ply6-unifen_Q.txt.dont_lsp" format=epd order=random plies=6 policy=default \
    -games 350 -concurrency 32 \
    -recover \
    -each tc=1 timemargin=200 proto=uci restart=on \
    -ratinginterval 10

for i in {0..100}; do
    echo
done

#    -engine "name=NB v6_transpos_order" cmd=./backups/v6_transpos_order \
#    -engine "name=NB v7_minimized_986" cmd=./backups/v7_minimized_986 \
