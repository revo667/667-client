{
  description = "667 Client DDNet";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
    in {
      packages.${system}.default = pkgs.stdenv.mkDerivation rec {
        pname = "667client";
        version = "3.0";

        src = pkgs.fetchurl {
          url = "https://github.com/revo667/667-client/releases/download/v${version}/667client-linux.tar.xz";
          hash = "sha256-esKwubZZ0LpVk9FffezuYZaX6YC5GvNNw3aHN0md3P4=";
        };

        nativeBuildInputs = [ 
          pkgs.autoPatchelfHook
          pkgs.makeWrapper
        ];

        buildInputs = [
          pkgs.stdenv.cc.cc.lib
          pkgs.SDL2
          pkgs.freetype
          pkgs.libGL
          pkgs.curl
          pkgs.openssl
	        pkgs.vulkan-loader
	        pkgs.libnotify
        ];

        # Keep URL and sourceRoot derived from `version` so future bumps
        # only need to touch `version` (plus the src hash, which Nix requires
        # to be pinned and can never be derived).
        sourceRoot = "667client-${version}-linux_x86_64";

        installPhase = ''
        mkdir -p $out/bin $out/share/applications
        cp -r . $out/
        chmod +x $out/DDNet

        cat > $out/share/applications/667client.desktop <<EOF
[Desktop Entry]
Name=667 Client
Comment=DDNet client with extra features
Exec=$out/bin/667client
Icon=$out/data/667Client/icon.png
Type=Application
Categories=Game;
EOF

        # wrap the binary so it runs from the right directory
        makeWrapper $out/DDNet $out/bin/667client \
          --run "cd $out"
        '';
      };

      apps.${system}.default = {
        type = "app";
        program = "${self.packages.${system}.default}/bin/667client";
      };
    };
}
