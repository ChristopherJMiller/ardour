{
  description = "Ardour DAW development environment";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.11";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in
      {
        devShells.default = pkgs.mkShell {
          name = "ardour-dev";

          # Tools needed to build but not link against
          nativeBuildInputs = with pkgs; [
            pkg-config
            python3
            perl
            doxygen
            graphviz
            itstool
            gettext
            # dev iteration / debugging
            gdb
            rr
            valgrind
            ccache
            clang-tools
          ];

          # Mirrors the buildInputs from nixpkgs's ardour (pkgs/by-name/ar/ardour/package.nix)
          # plus video support deps (harvid, xjadeo)
          buildInputs = with pkgs; [
            alsa-lib
            aubio
            boost
            cairomm
            cppunit
            curl
            dbus
            ffmpeg
            fftw
            fftwSinglePrec
            flac
            fluidsynth
            glibmm
            hidapi
            kissfft
            libarchive
            libjpeg
            libjack2
            liblo
            libltc
            libogg
            libpulseaudio
            librdf_rasqal
            libsamplerate
            libsigcxx
            libsndfile
            libusb1
            libuv
            libwebsockets
            libxml2
            libxslt
            lilv
            lrdf
            lv2
            pango
            pangomm
            qm-dsp
            readline
            rubberband
            serd
            sord
            soundtouch
            sratom
            suil
            taglib
            vamp-plugin-sdk
            xorg.libXinerama
            xorg.libXrandr
            # video support – wired up by gtk2_ardour at runtime via PATH
            harvid
            xjadeo
          ];

          # Ardour's wscript trips on stdenv's AS=as (tracker #8096); the nixpkgs
          # build patches it out, but in a dev shell we just unset it.
          LINKFLAGS = "-lpthread";

          shellHook = ''
            unset AS

            cat <<'EOF'
            Ardour dev shell ready.

              ./waf configure --test --debug-symbols   # one-time
              ./waf -j$(nproc)                         # build
              gtk2_ardour/ardev                        # run from build tree
              gtk2_ardour/ardbg                        # run under gdb
              gtk2_ardour/artest                       # run unit tests
            EOF
          '';
        };

        # Builds ardour from this fork (this flake's `self`) by overriding the
        # nixpkgs ardour derivation. Inherits all the upstream packaging — patches,
        # bundled content, wafConfigureFlags, postInstall icon/desktop install,
        # video-tool wrapping — and only swaps the source and version.
        #
        # The version string is the major version of our synthetic git tag; the
        # wscript reads MAJOR from `git describe` to name the installed binary,
        # so this produces `bin/ardour9`. mainProgram is updated to match.
        packages.default = pkgs.ardour.overrideAttrs (finalAttrs: old: {
          pname   = "ardour-mcp";
          version = "9.0-pre0";

          src = self;

          # Rewrite postPatch so the revision.cc stamp uses our version string
          # and we skip the upstream fetchgit-related steps that no longer apply.
          # Also strip any .git directory that might have ridden along in the
          # source — Ardour's wscript prefers `git describe` over the canned
          # revision.cc when it sees one, and we don't ship git in the sandbox.
          postPatch = ''
            rm -rf .git .gitattributes
            printf '#include "libs/ardour/ardour/revision.h"\nnamespace ARDOUR { const char* revision = "${finalAttrs.version}"; const char* date = ""; }\n' > libs/ardour/revision.cc
            sed 's|/usr/include/libintl.h|${pkgs.glibc.dev}/include/libintl.h|' -i wscript
            patchShebangs ./tools/
            substituteInPlace libs/ardour/video_tools_paths.cc \
              --replace-fail 'ffmpeg_exe = X_("");' 'ffmpeg_exe = X_("${pkgs.ffmpeg}/bin/ffmpeg");' \
              --replace-fail 'ffprobe_exe = X_("");' 'ffprobe_exe = X_("${pkgs.ffmpeg}/bin/ffprobe");'
          '';

          # Master added an #include <jpeglib.h> after 8.12 (see commit
          # e974a861ead). Upstream nixpkgs 8.12 didn't need libjpeg, so we add
          # it here.
          buildInputs = old.buildInputs ++ [ pkgs.libjpeg ];

          meta = old.meta // { mainProgram = "ardour9"; };
        });
      });
}
