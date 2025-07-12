# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a re-implementation of the 1995 PlayStation game wipEout, written in C. The project aims to recreate the classic futuristic racing game with modern portability while maintaining the original gameplay feel.

## Build Commands

### Using Make (Linux/macOS/MSYS2)
```bash
# Build with SDL2 backend (supports controllers, OpenGL 2.x)
make sdl

# Build with SDL2 and legacy GLX backend (for older NVIDIA drivers)
USE_GLX=true make sdl

# Build with Sokol backend (OpenGL 3.3, no controller support)
make sokol

# Build for web (Emscripten)
make wasm

# Debug build
DEBUG=true make sdl

# Clean build artifacts
make clean
```

### Using CMake (Cross-platform)
```bash
# Basic build
cmake -S . -B build
cmake --build build

# Platform-specific builds
cmake -S . -B build -DPLATFORM=SDL2    # or SOKOL, SWITCH
cmake -S . -B build -DRENDERER=GL      # or GLES2, SOFTWARE
cmake -S . -B build -DDEV_BUILD=ON     # Assets from source directory

# macOS with Homebrew
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix sdl2)"

# Web build
emcmake cmake -S . -B build -DPLATFORM=SOKOL
cmake --build build

# Nintendo Switch build (requires Docker)
./build-switch.sh
```

## Architecture Overview

### Core Systems Architecture
The codebase follows a layered architecture:

1. **Platform Layer** (`src/platform_*.c`): SDL2, Sokol, or Switch backends for windowing, input, and audio
2. **System Layer** (`src/system.c`): Core engine coordination, timing, and main loop
3. **Game Layer** (`src/wipeout/`): Game-specific logic, scenes, and gameplay

### Key Subsystems

- **Rendering** (`src/render_*.c`): OpenGL/software renderer with multiple backends
- **Asset Management** (`src/wipeout/image.c`, `object.c`): Loads PSX-format assets (TIM, PRM, CMP)
- **Audio** (`src/wipeout/sfx.c`): VAG sound effects and QOA music playback
- **Physics** (`src/wipeout/ship.c`, `ship_player.c`): Anti-gravity racing physics
- **AI** (`src/wipeout/ship_ai.c`): Computer-controlled racers
- **Track System** (`src/wipeout/track.c`): Track loading and collision detection
- **Game State** (`src/wipeout/game.c`): Scene management and game progression

### Scene System
The game uses a scene-based state machine:
- `GAME_SCENE_INTRO`: Intro video playback
- `GAME_SCENE_TITLE`: Title screen
- `GAME_SCENE_MAIN_MENU`: Main menu navigation
- `GAME_SCENE_RACE`: Active racing gameplay
- `GAME_SCENE_HIGHSCORES`: Score display

### Memory Management
Custom memory allocator with two allocation types:
- `mem_bump()`: Permanent allocations for game data
- `mem_temp_alloc()`: Temporary allocations for loading/processing

## Asset System

### Expected Directory Structure
```
./wipegame                    # Executable
./wipeout/                    # Asset directory
├── common/                   # Shared models and textures
├── textures/                 # UI textures and fonts
├── sound/                    # Sound effects (VB format)
├── music/                    # Background music (QOA format)
├── intro.mpeg               # Intro video
└── track01-14/              # Track-specific assets
    ├── library.cmp          # Compressed textures
    ├── scene.prm            # 3D models
    ├── track.trf/.trs/.trv  # Track geometry
    └── ...
```

### Asset Formats
- **TIM**: PlayStation texture format (4/8/16-bit)
- **CMP**: LZSS-compressed texture collections
- **PRM**: PlayStation 3D model format
- **VB**: VAG audio format for sound effects
- **QOA**: Modern audio format for music
- **MPEG**: Video files for intro sequences

## Code Conventions

### File Organization
- Platform-specific code: `platform_*.c`
- Rendering backends: `render_*.c`
- Game modules: `src/wipeout/*.c`
- Headers mirror source files: `*.h` for `*.c`

### Naming Conventions
- Functions: `snake_case`
- Types: `snake_case_t` suffix
- Enums: `UPPER_CASE`
- Constants/defines: `UPPER_CASE`

### Key Data Structures
- `game_t g`: Global game state
- `save_t save`: Persistent save data
- `ship_t ships[]`: All ships in race
- `track_t track`: Current track data
- `camera_t camera`: View/projection state

## Development Notes

### Renderer Selection
The game supports multiple rendering backends:
- OpenGL 3.3 (default) - Modern pipeline
- OpenGL ES 2.0 - Mobile compatibility
- Software renderer - CPU-only fallback

### Platform Differences
- SDL2: Full controller support, OpenGL 2.x compatibility
- Sokol: Modern graphics, limited to desktop/web platforms
- Switch: Nintendo Switch homebrew, OpenGL ES 2.0, custom controls
- Web: Emscripten build with asset preloading

### Performance Considerations
- All geometry is constructed at draw time (no GPU-side buffers)
- No frustum culling implemented
- Everything within fadeout radius is drawn
- Custom memory allocator reduces malloc overhead

## Testing

No automated test suite exists. Testing involves:
1. Building for target platform
2. Ensuring game assets are properly located
3. Manual gameplay testing across different scenarios
4. Performance verification on target hardware

## Original Game Compatibility

This rewrite targets the PSX NTSC version but requires some PC version menu models. The game expects original asset files that are not included in this repository due to copyright restrictions.