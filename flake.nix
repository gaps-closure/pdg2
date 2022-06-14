{
    description = "A program dependence graph";

    # Nixpkgs / NixOS version to use.
    inputs.nixpkgs.url = "nixpkgs";

    outputs = { self, nixpkgs }:
        let
            # Generate a user-friendly version number.
            version = builtins.substring 0 8 self.lastModifiedDate;

            # System types to support.
            supportedSystems = [ "x86_64-linux" "x86_64-darwin" "aarch64-linux" "aarch64-darwin" ];

            # Helper function to generate an attrset '{ x86_64-linux = f "x86_64-linux"; ... }'.
            forAllSystems = nixpkgs.lib.genAttrs supportedSystems;

            # Nixpkgs instantiated for supported system types.
            nixpkgsFor = forAllSystems (system: import nixpkgs { inherit system; });
        in
        {
            packages = forAllSystems (system: 
                let 
                    pkgs = nixpkgsFor.${system};
                    deps = with pkgs; [ cmake llvmPackages_10.llvm ];
                in
                {
                    # Build pdg derivation using cmake as default package
                    default = with pkgs; stdenv.mkDerivation {
                        pname = "pdg";
                        inherit version;
                        src = ./.;
                        buildInputs = deps; 
                    };
                }
            );
        };
}