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

        # `nix build` produces the upstream nixpkgs ardour, handy as a smoke test.
        packages.default = pkgs.ardour;
      });
}
