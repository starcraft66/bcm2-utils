{ lib
, gcc13Stdenv
, boost182
}:

gcc13Stdenv.mkDerivation {
  pname = "bcm2-utils";
  version = "nightly";

  src = ./.;

  buildInputs = [
    boost182
  ];

  installPhase = ''
    mkdir -p $out/bin
    install -m 755 bcm2cfg $out/bin
    install -m 755 bcm2dump $out/bin
    install -m 755 psextract $out/bin
    install -m 755 bcm2boltenv $out/bin
  '';

  meta = {
    description = "Nix language server";
    homepage = "https://github.com/nix-community/nixd";
    license = lib.licenses.lgpl3Plus;
    maintainers = with lib.maintainers; [ inclyc ];
    platforms = lib.platforms.unix;
  };
}

