{
  description = "Venta";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system: let
      pkgs = nixpkgs.legacyPackages.${system};
      ver = "1.1";
    in {
      packages.default = pkgs.stdenv.mkDerivation {
        pname = "venta";
        version = ver;
        src = ./.;
        nativeBuildInputs = [ pkgs.makeWrapper pkgs.bash ];
        buildPhase = "true";
        installPhase = ''
          mkdir -p $out/bin
          cp venta.sh $out/bin/venta
          chmod +x $out/bin/venta

          wrapProgram $out/bin/venta \
            --prefix PATH : ${pkgs.lib.makeBinPath [ pkgs.bash pkgs.gawk pkgs.coreutils ]} \
            --set VENTA_VERSION "${ver}"
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
