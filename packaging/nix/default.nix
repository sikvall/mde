{ lib
, stdenv
, ncurses
, libzip
, pkg-config
}:

stdenv.mkDerivation rec {
  pname = "mde";
  version = "0.1.3";

  src = ../..;

  nativeBuildInputs = [ pkg-config ];
  buildInputs = [ ncurses libzip ];

  configureFlags = [ "--disable-werror" ];

  doCheck = true;

  meta = with lib; {
    description = "Full-screen text-mode Markdown editor with native EPUB 2/3 exporter";
    homepage = "https://github.com/sikvall/mde";
    license = licenses.mit;
    platforms = platforms.unix;
    mainProgram = "mde";
  };
}
