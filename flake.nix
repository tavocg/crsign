# This file is part of CRSign.
# Copyright (C) 2026 Gustavo Calvo <tavo@tavo.cr>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# See sign/main.c for the full copyright and warranty notice.
# See LICENSE in the project root for the GNU General Public License.

{
  description = "CRSign: project for the Sign digital signing application";

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
          openscPin = builtins.fromJSON (builtins.readFile ./packaging/opensc.json);
          embeddedOpenSC = pkgs.stdenv.mkDerivation {
            pname = "crsign-opensc";
            version = openscPin.revision;
            src = pkgs.fetchurl {
              inherit (openscPin) url sha256;
              name = "opensc-${openscPin.revision}.tar.gz";
            };
            nativeBuildInputs = with pkgs; [ autoconf automake libtool pkg-config python3 ];
            buildInputs = with pkgs; [ openssl pcsclite ];
            dontConfigure = true;
            PCSC_CFLAGS = "-I${pkgs.lib.getDev pkgs.pcsclite}/include/PCSC";
            PCSC_PROVIDER = "${pkgs.lib.getLib pkgs.pcsclite}/lib/libpcsclite.so.1";
            buildPhase = ''
              python ${./scripts/build-opensc.py} --source "$PWD" \
                --build "$PWD/provider-build" --prefix "$out"
              cp "$src" "$out/share/crsign-deps/opensc-${openscPin.revision}.tar.gz"
              cp ${./packaging/opensc.json} "$out/share/crsign-deps/opensc-source.json"
            '';
            dontInstall = true;
          };
        in
        {
          default = pkgs.mkShell {
            name = "crsign";
            LOCALE_ARCHIVE = "${pkgs.glibcLocales}/lib/locale/locale-archive";

            packages = with pkgs; [
              gcc clang-tools cmake poppler-utils xvfb-run xdotool
              (python3.withPackages (pythonPkgs: with pythonPkgs; [
                cryptography lxml jsonschema pyhanko
              ]))
            ];
            nativeBuildInputs = with pkgs; [ pkg-config gettext ];
            buildInputs = with pkgs; [
              gtk4 libadwaita poppler glib librsvg gdk-pixbuf cairo tomlc17
              curl openssl p11-kit file qpdf zlib jansson libxml2
            ] ++ [ embeddedOpenSC ];

            shellHook = ''
              # Unwrapped GTK binaries also need the schema paths collected by
              # Nix's GLib setup hook, including on runners without a desktop.
              if [[ -n "''${GSETTINGS_SCHEMAS_PATH:-}" ]]; then
                export XDG_DATA_DIRS="$GSETTINGS_SCHEMAS_PATH''${XDG_DATA_DIRS:+:$XDG_DATA_DIRS}"
              fi

              if [[ $- == *i* ]]; then
                PS1="(crsign) $PS1"
              fi
            '';
          };
        });
    };
}
