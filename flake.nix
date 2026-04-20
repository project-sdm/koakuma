{
  description = "A simple DBMS written in C++.";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs?ref=nixos-unstable";
  };

  outputs =
    { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };
      inherit (pkgs) lib;
    in
    {
      packages.${system} = {
        default = self.packages.${system}.koakuma;

        koakuma = pkgs.stdenv.mkDerivation {
          pname = "koakuma";
          version = "main";

          src = lib.cleanSource ./.;

          nativeBuildInputs = with pkgs; [
            cmake
          ];

          enableParallelBuilding = true;
        };
      };

      devShells.${system}.default = pkgs.mkShell {
        inputsFrom = [ self.packages.${system}.default ];
        packages = with pkgs; [
          valgrind
        ];
      };
    };
}
