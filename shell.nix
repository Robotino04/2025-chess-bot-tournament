let
  pkgs = import <nixpkgs> {};
  chess-lib = pkgs.stdenv.mkDerivation {
    version = "0.0.1";
    pname = "chess";
    src = ./src/c/.;
    buildPhase = ''
      mkdir -p $out/lib
      $CC -o $out/lib/libchess.so -shared bitboard.c chessapi.c -fPIC
    '';
  };
in
  pkgs.mkShell {
    packages = [
      chess-lib
    ];
  }
