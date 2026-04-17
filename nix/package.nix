{ lib, stdenv, fetchFromGitHub, fastfetch, makeWrapper }:

stdenv.mkDerivation (finalAttrs: {
  pname = "fetch";
  version = "1.0.0-unstable-18-04-1";


# Uncomment to build locally
src = ../.;

# Uncomment and change rev and hash when upstream fix for kitty has been pushed
#src = fetchFromGitHub {
#  owner = "areofyl";
#  repo = "fetch";
#  rev = "a056f655a57611cba4f1076cfa40dbf360da164f";
#  hash = "sha256-508QYmyPSKZisABowgyaRV93ZrkTiS3ziKBCcs3WOpQ=";
#};


  # Install in nix/store
  makeFlags = [ "PREFIX=${placeholder "out"}" ];

  meta = {
    description = "Animated 3D fetch tool that renders your distro logo as a spinning bas-relief";
    homepage = "https://github.com/areofyl/fetch";
    license = lib.licenses.isc;
    maintainers = with lib.maintainers; [ areofyl ];
    mainProgram = "fetch";
    platforms = lib.platforms.linux;
  };
})
