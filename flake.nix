{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    flake-parts.url = "github:hercules-ci/flake-parts";
  };

  outputs = { nixpkgs, flake-parts, ... }@inputs: flake-parts.lib.mkFlake { inherit inputs; } {
    imports = [
      inputs.flake-parts.flakeModules.easyOverlay
    ];
    perSystem = { config, self', inputs', pkgs, system, ... }:
      with pkgs;
      let
        bcm2-utils = callPackage ./default.nix { };
      in
      {
        packages.default = bcm2-utils;
        overlayAttrs = {
          inherit (config.packages) bcm2-utils;
        };
        packages.bcm2-utils = bcm2-utils;

        devShells.default = pkgs.mkShell {
          buildInputs = [
            boost182
          ];
        };
      };
    systems = nixpkgs.lib.systems.flakeExposed;
  };
}

