{
  description = "Independent C/C++ incremental REPL built on the Clang interpreter";
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin" ];
      eachSystem = nixpkgs.lib.genAttrs systems;
      packageFor = system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          llvm = pkgs.llvmPackages_21;
        in llvm.stdenv.mkDerivation {
          pname = "crepl";
          version = "0.1.0";
          src = pkgs.lib.cleanSourceWith {
            src = ./.;
            filter = path: type:
              !(builtins.elem (builtins.baseNameOf path) [ "build" ".git" ".direnv" "result" "compile_commands.json" ]);
          };
          nativeBuildInputs = [ pkgs.cmake pkgs.ninja pkgs.python3 ];
          buildInputs = [ llvm.llvm.dev llvm.libclang.dev llvm.libclang ];
          cmakeFlags = [
            "-DLLVM_DIR=${llvm.llvm.dev}/lib/cmake/llvm"
            "-DClang_DIR=${llvm.libclang.dev}/lib/cmake/clang"
          ];
          doCheck = true;
          checkPhase = "ctest --output-on-failure";
          meta = {
            description = "Stateful incremental C/C++ interpreter";
            mainProgram = "crepl";
            platforms = pkgs.lib.platforms.unix;
          };
        };
    in {
      packages = eachSystem (system: let package = packageFor system; in {
        default = package;
        crepl = package;
      });
      checks = eachSystem (system: { crepl = self.packages.${system}.default; });
      apps = eachSystem (system: {
        default = { type = "app"; program = "${self.packages.${system}.default}/bin/crepl"; };
      });
      devShells = eachSystem (system:
        let pkgs = nixpkgs.legacyPackages.${system}; llvm = pkgs.llvmPackages_21;
        in {
          default = (pkgs.mkShell.override { stdenv = llvm.stdenv; }) {
            name = "crepl-development";
            nativeBuildInputs = [ pkgs.cmake pkgs.ninja pkgs.python3 pkgs.just pkgs.doxygen pkgs.graphviz pkgs.pkg-config pkgs.direnv ];
            buildInputs = [ llvm.llvm.dev llvm.libclang.dev llvm.libclang ];
            LLVM_DIR = "${llvm.llvm.dev}/lib/cmake/llvm";
            Clang_DIR = "${llvm.libclang.dev}/lib/cmake/clang";
          };
        });
    };
}
