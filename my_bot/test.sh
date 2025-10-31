#!/usr/bin/env bash
cutechess-cli \
    -engine "name=NB Latest" cmd=./example_bot \
    -engine "name=NB v9_negascout_tc" cmd=./backups/v9_negascout_tc \
    -openings file="UHO_Lichess_4852_v1.epd" format=epd order=random policy=default \
    -games 999999 -concurrency 32 \
    -each tc=1 timemargin=200 proto=uci restart=on \
    -ratinginterval 10 \
    -sprt elo0=0 elo1=10 alpha=0.05 beta=0.05

#    -engine "name=NB v6_transpos_order" cmd=./backups/v6_transpos_order \
#    -engine "name=NB v7_minimized_986" cmd=./backups/v7_minimized_986 \
