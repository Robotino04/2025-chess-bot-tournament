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
