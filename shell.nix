let
  pkgs = import <nixpkgs> {};
  chess-lib = pkgs.stdenv.mkDerivation {
    version = "0.0.2";
    pname = "chess";
    src = pkgs.fetchFromGitHub {
      owner = "shiro-nya";
      repo = "2025-chess-bot-tournament";
      rev = "main";
      hash = "sha256-cG7CvV/r5IixqO0pHfY/b4lgXR0M2qq8viUUGIJaKx0=";
    };
    dontStrip = true;
    buildPhase = ''
      cd src/c/
      mkdir -p $out/{lib,include}
      $CC -O3 -g -o $out/lib/libchess.so -shared bitboard.c chessapi.c -fPIC
      cp *.h $out/include/
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
      pkgs.openjdk25

      pkgs.cutechess
      pkgs.stockfish

      pkgs.llvmPackages.clang-tools
      pkgs.llvmPackages.clang
      pkgs.llvmPackages.openmp
      pkgs.uncrustify
      (pkgs.python3.withPackages pythonPkgs)

      pkgs.gdb
      pkgs.cargo
      pkgs.libclang
      pkgs.rustc
      pkgs.samply
      pkgs.gnuplot
      pkgs.feedgnuplot
    ];

    LD_LIBRARY_PATH = pkgs.lib.makeLibraryPath packages;

    shellHook = ''
      unset NIX_ENFORCE_NO_NATIVE
    '';
  }
