{
  description = "ESP32 + Waveshare 64x32 HUB75 matrix audio visualizer (arduino-cli)";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in
    {
      devShells = forAllSystems (pkgs:
        let
          # The ESP32 core downloads prebuilt, dynamically linked toolchains
          # (xtensa gcc, esptool, ...) that can't run on NixOS directly. Wrapping
          # arduino-cli in an FHS environment means every tool it spawns runs
          # with a conventional /lib and /usr layout.
          arduino-cli-fhs = pkgs.buildFHSEnv {
            name = "arduino-cli";
            targetPkgs = p: with p; [
              arduino-cli
              (python3.withPackages (ps: [ ps.pyserial ]))
              zlib
              libusb1
              udev
              ncurses
              stdenv.cc.cc.lib
            ];
            runScript = "arduino-cli";
          };
        in
        {
          default = pkgs.mkShell {
            packages = [ arduino-cli-fhs pkgs.gnumake ];

            shellHook = ''
              # Keep cores, toolchains and libraries inside the project.
              export ARDUINO_DIRECTORIES_DATA="$PWD/.arduino/data"
              export ARDUINO_DIRECTORIES_DOWNLOADS="$PWD/.arduino/staging"
              export ARDUINO_DIRECTORIES_USER="$PWD/.arduino/user"
              export ARDUINO_BOARD_MANAGER_ADDITIONAL_URLS="https://espressif.github.io/arduino-esp32/package_esp32_index.json"
              export ARDUINO_UPDATER_ENABLE_NOTIFICATION=false

              echo "led-matrix-audio-visualizer dev shell"
              echo "  make setup    install ESP32 core + libraries (first run)"
              echo "  make build    compile"
              echo "  make upload   compile + flash (override with PORT=...)"
              echo "  make monitor  serial monitor"
            '';
          };
        });
    };
}
