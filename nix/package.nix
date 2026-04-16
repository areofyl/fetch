{ lib, stdenv, fetchFromGitHub, fastfetch, makeWrapper }:

stdenv.mkDerivation (finalAttrs: {
  pname = "fetch";
  version = "1.0.0";

# Uncomment to build locally
# src = ../.;

src = fetchFromGitHub {
  owner = "areofyl";
  repo = "fetch";
  rev = "v${finalAttrs.version}";
  hash = "sha256-rZr5ScxCr0L70LTpkntanDN46vjuqPGm2SMwqXewU0g=";
};

  # Install in nix/store
  makeFlags = [ "PREFIX=${placeholder "out"}" ];

  # Wrap fastfetch so it has the correct nix/store path
  nativeBuildInputs = [ makeWrapper ];
  postInstall = ''
    wrapProgram $out/bin/fetch \
      --prefix PATH : ${lib.makeBinPath [ fastfetch ]}
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
