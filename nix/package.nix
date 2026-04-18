{ lib, stdenv, fetchFromGitHub, fastfetch, pciutils, makeWrapper }:

stdenv.mkDerivation (finalAttrs: {
  pname = "fetch";
  version = "1.0.0-unstable-18-04-2";


  # Uncomment to build locally
  # src = ../.;

  src = fetchFromGitHub {
    owner = "areofyl";
    repo = "fetch";
    rev = "67ba7abd90d548e89a0a60bf7a850ee41f5c6f79";
    hash = "sha256-20lZPymAgXmTAxxBs75E9es1ChHZ4RSS73Qb/SzYeiI=";
  };


  # Install in nix/store
  makeFlags = [ "PREFIX=${placeholder "out"}" ];

  # Wrap fastfetch so it has the correct nix/store path
  nativeBuildInputs = [ makeWrapper ];
  postInstall = ''
    wrapProgram $out/bin/fetch \
    --prefix PATH : ${lib.makeBinPath [ fastfetch pciutils ]}
  '';

  meta = {
    description = "Animated 3D fetch tool that renders your distro logo as a spinning bas-relief";
    homepage = "https://github.com/areofyl/fetch";
    license = lib.licenses.isc;
    maintainers = with lib.maintainers; [ areofyl ];
    mainProgram = "fetch";
    platforms = lib.platforms.linux;
  };
})
