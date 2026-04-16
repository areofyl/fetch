{
  description = "Animated 3D fetch tool for your terminal";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";  # Can be overwritten in inputs with nixpkgs.follows
  };

  outputs =
    {
      self,
      nixpkgs
    }:

    # Only tested on Linux x86_64
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in
    {
      packages = forAllSystems (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in
        {
          default = pkgs.callPackage ./nix/package.nix { }; # Change this if repo author doesn't like the directory structure
        }
      );

      overlays.default = final: prev: {
        areofyl-fetch = final.callPackage ./nix/package.nix { }; # ... and this
      };
      # Make home-manager options available
      homeManagerModules.default = import ./nix/home-module.nix; # ... and this :-)
    };
}
