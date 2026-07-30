{
  description = "Venta";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system: let
      pkgs = nixpkgs.legacyPackages.${system};
      ver = "2.0";
    in {
      packages.default = pkgs.stdenv.mkDerivation {
        pname = "venta";
        version = ver;
        src = ./.;
        nativeBuildInputs = [ pkgs.gcc ];
        buildPhase = ''
          gcc -std=c11 -O2 -Wall -Wextra -o venta venta.c -lm -DVENTA_VERSION='"${ver}"'
        '';
        installPhase = ''
          mkdir -p $out/bin
          cp venta $out/bin/venta
        '';
        meta = with pkgs.lib; {
          description = "DNA simulation in terminal with corruption,recovery,chaos";
          license = licenses.mit;
          platforms = platforms.linux;
          mainProgram = "venta";
        };
      };

      apps.default = {
        type = "app";
        program = "${self.packages.${system}.default}/bin/venta";
      };
    });
}
