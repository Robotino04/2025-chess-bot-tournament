# Thera-Mini
My submission to the [Neuro-sama Headquarters Chess Bot Tournament](https://github.com/shiro-nya/2025-chess-bot-tournament).

A classical chess engine with pretty average features:
- Aspiration windows
- History heuristic
- Iterative deepening
- Late move reduction
- Material-based static evaluation
- Move ordering
- NegaScout/PVS
- Quiescence search
- Some statistics (sadly disabled for tokens)
- Transposition table

Even though it shares a name with my "big" chess engine [Thera](https://github.com/Robotino04/Thera-Engine) (which is also terrible), it was written from scratch. I just couldn't think of a better name :3 

All macros are generated using my own minimizer and a greedy algorithm. Most of that is unreadable though; probably because at least 30% is vibe coded. Said minimizer also formats the code in Neuros signature gear shape.

The neural network code wasn't used in the end because it didn't significantly improve upon the static evaluation.

## Notes
<https://webdocs.cs.ualberta.ca/~mmueller/courses/2014-AAAI-games-tutorial/slides/AAAI-14-Tutorial-Games-3-AlphaBeta.pdf>
<https://medium.com/@waadlingaadil/learn-to-build-a-neural-network-from-scratch-yes-really-cac4ca457efc>

Lichess preprocessing before the NN training code can read it.
```sh
nix run nixpkgs#pv -- -r --eta lichess_db_eval.jsonl | jq -r '
  select(.evals != null and (.evals | length > 0)) |
  . as $root |
  ($root.evals | max_by(.depth)) as $best |
  select($best.pvs[0].cp != null) |
  "\($root.fen)\n\($best.depth)\n\($best.pvs[0].cp)"
' > lichess_db_eval_processed.raw
```
