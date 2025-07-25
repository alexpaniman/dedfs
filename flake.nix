{
  inputs = {
    nixpkgs.url = github:NixOS/nixpkgs;
    flake-utils.url = github:numtide/flake-utils;
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let 
        pkgs = import nixpkgs { inherit system; };
      in {
        packages.default = pkgs.stdenv.mkDerivation {
          name = "dedfs";
          src = ./.;

          nativeBuildInputs = with pkgs; [ cmake ninja ];
          buildInputs = with pkgs; [ gcc fuse ];
        };
      }
    );
}
