{
  description = "crsign: digital signing application";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { nixpkgs, ... }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in
    {
      devShells = forAllSystems (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in
        {
          default = pkgs.mkShell {
            name = "crsign";

            packages = with pkgs; [ gcc clang-tools cmake ];
            nativeBuildInputs = with pkgs; [ pkg-config ];
            buildInputs = with pkgs; [ gtk4 ];

            shellHook = ''
              if [[ $- == *i* ]]; then
                PS1="(crsign) $PS1"
              fi
            '';
          };
        });
    };
}
