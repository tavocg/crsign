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
              gtk4 libadwaita glib librsvg gdk-pixbuf cairo tomlc17 curl openssl p11-kit file qpdf zlib jansson libxml2
              # FindPkgConfig also queries GTK private dependencies for static linking.
              libsysprof-capture pcre2 util-linux libselinux libsepol
              fribidi libthai libdatrie libxdmcp libxau dav1d appstream
            ];

            shellHook = ''
              export CMAKE_PREFIX_PATH="$HOME/Projects/libssig/build''${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"

              if [[ $- == *i* ]]; then
                PS1="(crsign) $PS1"
              fi
            '';
          };
        });
    };
}
