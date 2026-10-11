<div align="center">
  <h1>667 Client</h1>
  <p>DDNet client focused on performance, comfort and competitive quality-of-life features. macOS first, Windows and Linux supported.</p>

  <p>
    <a href="https://www.revo667.com">Website</a> ·
    <a href="https://discord.gg/667">Discord</a> ·
    <a href="https://github.com/revo667/667-client/releases/latest">Download</a>
  </p>
</div>

## Features

### Visuals

- **Chat Media** — inline photo/GIF previews with a domain filter
- **Hook effects** — air jump ring, hammer slash, hook lasso and hook hold energy
- **Weapon glow** — laser, shotgun and grenade glow
- **3D Particles** — cubes/hearts/mixed, glow, player push
- **Weather** — snow, rain, fireflies and ambient dust
- **Media Background** — video/image in menus and in game
- **Animations** — UI reveal, chat, killfeed and main menu animations
- **Music Player** — HUD player with visualizer and synced lyrics
- **Keystrokes** — Classic / Minecraft keyboard and mouse HUD
- **Custom Aspect Ratio** — presets and arbitrary ratios
- **Chat Bubbles** — bubbles above players

Demo playback only: **Camera Drift**, **Dynamic FOV**.

### Gameplay

- **Inputs** — Fast, Best, Saiko, Delta, F and Cloud input prediction, Auto Margin
- **Snap Tap** — instant opposite left/right switching
- **Optimizer** — disable particles/quads, FPS fog
- **Gores mode** — entity-like mode with auto-disable while holding weapons
- **Fast Actions** — quick command selector
- **Finish Prediction** — finish progress prediction on race maps
- **Focus Mode** — hide distracting HUD/UI elements
- **Fast Practice** — local practice world with practice chat commands

### Interface

- **HUD Editor** — drag, drop and resize HUD modules
- **Better UI** — themed checkboxes, scrollbars, tabs, color pickers and per-element colors
- **Settings search**, **Assets Editor**, **Admin Panel**, **Translate**
- **Server browser** — live map preview, auto refresh, short EGO/KoG names
- **Mini games** — Casino, Snake, Minesweeper, Chess, Memory, Pong, Brick Breaker, Sudoku

## Build

```bash
git submodule update --init --recursive
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target game-client
```

Run tests:

```bash
cmake --build build --target run_tests
```

See [docs/BUILDING.md](docs/BUILDING.md) for platform specific instructions.

## Credits

667 Client is a fork of [BestClient](https://github.com/BestProjectTeam/BestClient), which builds on [TClient](https://github.com/sjrc6/TaterClient-ddnet) and [DDNet](https://github.com/ddnet/ddnet).

Some features were adapted from [Entity-Client](https://github.com/FoxNet-DDNet/Entity-Client-DDNet) (Better Spectate, Chat Bubbles) and [RushieClient](https://github.com/RushieClient/RushieClient-ddnet) (Chat Filter, Find Teleport, Find Finish).

## License

Code inherited from BestClient is released under the [MIT License](LICENSE).

Upstream DDNet/Teeworlds code, third-party libraries and assets under `data/` remain under their original licenses — see [license.txt](license.txt) and the per-directory `license.txt` files.
