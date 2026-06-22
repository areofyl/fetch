{
  lib,
  stdenv,
  fetchFromGitHub,
  # linux dependencies
  makeWrapper,
  fastfetch,
  pciutils
}:

stdenv.mkDerivation (finalAttrs: {
  pname = "fetch";
  version = "2.1.0";

  src = fetchFromGitHub {
    owner = "areofyl";
    repo = "fetch";
    rev = "v${finalAttrs.version}";
    hash = "sha256-9ixx7XJcY4ktcN/lUfjvFljvHIEO2ktOebeGgL0ulHg=";
  };

  makeFlags = [ "PREFIX=${placeholder "out"}" ];
  nativeBuildInputs = [ makeWrapper ];
  postInstall = ''
    wrapProgram $out/bin/fetch \
    --prefix PATH : ${lib.makeBinPath [ fastfetch pciutils ]}
  '';

  meta = {
    description = "Animated 3D fetch tool that renders your distro logo as a spinning bas-relief";
    homepage = "https://github.com/areofyl/fetch";
    license = lib.licenses.isc;
    maintainers = with lib.maintainers; [ ghastrum ];
    mainProgram = "fetch";
    platforms = lib.platforms.linux;
  };
})
