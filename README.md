<h1 align="center">
	VENTA
</h1>
A mininal dna simulator with corruption,recovery,chaos mainly made to be just something to use in a screenshot 


<h1 align="center">
	Showcase
</h1>

![Demo](.assets/venta.gif)


<h1 align="center">
	INSTALLATION
</h1>

<details><summary><b>Nixos Linux</b></summary>

#### Flakes + Home Manager
Into flake.nix add 
```venta.url = "github:realnrxg/venta";```
And into outputs add venta
exmp ```outputs = {self, nixpkgs, home-manager, venta, ...}:```

Into home.nix add
```{ pkgs, venta,  ... }:```
and into

```home.packages = with pkgs; [
venta.packages.${pkgs.stdenv.hostPlatform.system}.default
]```
