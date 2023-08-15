{
    description = "A program dependence graph";

    # Nixpkgs / NixOS version to use.
    inputs.nixpkgs.url = "nixpkgs";

    # Flake utils
    inputs.flake-utils.url = "github:numtide/flake-utils";

    outputs = { self, nixpkgs, flake-utils }:
        flake-utils.lib.eachDefaultSystem
        (system:

            let pkgs = nixpkgs.legacyPackages.${system}; 
                version = "3.0.0";
                deps = with pkgs; [ cmake llvmPackages_14.llvm ];
                pdg  = with pkgs; stdenv.mkDerivation {
                    pname = "pdg";
                    inherit version;
                    src = ./.;
                    buildInputs = [ cmake llvmPackages_14.llvm ]; 
                };
                svf  = with pkgs; stdenv.mkDerivation {
                    pname = "svf";
                    inherit version;
                    src = ./svf;
                    buildInputs = [ cmake llvmPackages_14.llvm z3 ];
                };
            in
            {
                packages = {
                    default = with pkgs; stdenv.mkDerivation {
                        pname = "pdg2";
                        inherit version;
                        src = ./.;
                        buildInputs = [ pdg svf ];
                    };
                };
                devShells = {
                    default = with pkgs; pkgs.mkShell {
                        packages = [ llvmPackages_14.llvm pdg svf ];
                        shellHook = ''
                            export PDG=${pdg.out};
                            export SVF=${svf.out};
                        '';
                    };
                };
            }
        );
}