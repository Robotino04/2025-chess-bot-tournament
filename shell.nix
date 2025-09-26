let
  pkgs = import <nixpkgs> {};
  chess-lib = pkgs.stdenv.mkDerivation {
    version = "0.0.2";
    pname = "chess";
    src = ./src/c/.;
    buildPhase = ''
      mkdir -p $out/lib
      $CC -O3 -o $out/lib/libchess.so -shared bitboard.c chessapi.c -fPIC
    '';
  };
in
  pkgs.mkShell {
    packages = [
      chess-lib
      pkgs.clang-tools
      pkgs.uncrustify
      pkgs.openjdk24
      (pkgs.python3.withPackages (ps: [
        ps.libclang

        ps.pcpp
        ps.ply

        ps.tqdm
        ps.rich
      ]))
    ];
  }
