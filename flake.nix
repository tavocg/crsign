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
          staticJansson = pkgs.jansson.overrideAttrs (old: {
            cmakeFlags = (old.cmakeFlags or []) ++ [ "-DJANSSON_BUILD_SHARED_LIBS=OFF" ];
          });
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
              gtk4 libadwaita poppler glib librsvg gdk-pixbuf cairo tomlc17 curl openssl p11-kit file qpdf zlib zlib.static staticJansson libxml2
              # FindPkgConfig also queries GTK private dependencies for static linking.
              libsysprof-capture pcre2 util-linux libselinux libsepol
              fribidi libthai libdatrie libxdmcp libxau dav1d appstream
            ];

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
