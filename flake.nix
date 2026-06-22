{
  description = "Animated 3D fetch tool for your terminal";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    {
      self,
      nixpkgs
    }:

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
          default = pkgs.callPackage ./nix/package.nix { };
        }
      );

      overlays.default = final: prev: {
        areofyl-fetch = final.callPackage ./nix/package.nix { };
      };
      homeManagerModules.default = import ./nix/home-module.nix;
    };
}
