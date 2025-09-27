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
  pythonPkgs = ps: [
    ps.libclang

    ps.pcpp
    ps.ply

    ps.tqdm
    ps.rich
  ];
in
  pkgs.mkShell rec {
    packages = [
      chess-lib
      pkgs.clang-tools
      pkgs.uncrustify
      pkgs.openjdk24
      (pkgs.python3.withPackages pythonPkgs)
      pkgs.cargo
      pkgs.libclang
      pkgs.rustc
    ];

    LD_LIBRARY_PATH = pkgs.lib.makeLibraryPath packages;
  }
