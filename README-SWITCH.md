# Nintendo Switch Port

This document describes how to build and install Wipeout Rewrite for the Nintendo Switch using libnx and Docker.

## Prerequisites

### Development Environment
- **Docker** and **docker-compose** installed on your system
- Access to a Nintendo Switch running Custom Firmware (CFW)
- Homebrew Launcher installed on your Switch

### Required Assets
You will need the original Wipeout game assets:
- PSX NTSC game data
- Some PC version menu models
- Music files converted to QOA format (optional)

## Building

### Using the Build Script (Recommended)
```bash
# Make the script executable and run it
chmod +x build-switch.sh
./build-switch.sh
```

### Manual Docker Build
```bash
# Build the Docker image
docker-compose -f docker-compose.switch.yml build

# Run the build
docker-compose -f docker-compose.switch.yml run --rm switch-builder bash -c "
    cmake -S . -B build-switch \
        -DPLATFORM=SWITCH \
        -DRENDERER=GLES2 \
        -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake \
        -DCMAKE_BUILD_TYPE=Release
    cmake --build build-switch
"
```

### Build Output
The build process will generate:
- `wipeout.nro` - The Nintendo Switch homebrew executable
- `wipeout.nacp` - Application metadata

## Installation

### Preparing Your SD Card
1. Copy the `wipeout.nro` file to `/switch/` on your SD card
2. Create the directory structure `/switch/wipeout/` on your SD card
3. Copy your game assets to `/switch/wipeout/` following this structure:

```
/switch/wipeout/
├── common/          # Shared game objects and textures
├── textures/        # UI textures and fonts  
├── sound/           # Sound effects (VB format)
├── music/           # Background music (QOA format) - optional
├── intro.mpeg       # Intro video (plays on startup)
└── track01-14/      # Individual track data
    ├── library.cmp
    ├── library.ttf
    ├── scene.cmp
    ├── scene.prm
    ├── sky.cmp
    ├── sky.prm
    ├── track.trf
    ├── track.trs
    └── track.trv
```

### Launching the Game
1. Insert your SD card into the Nintendo Switch
2. Launch the Homebrew Launcher (via album or other method)
3. Navigate to and launch "Wipeout Rewrite"

## Controls

The game uses the standard Nintendo Switch controls:

### Racing Controls
- **Left Stick**: Steering
- **ZR**: Thrust/Accelerate
- **ZL**: Brake  
- **L/R**: Air brakes (left/right)
- **A**: Fire weapon
- **X**: Change camera view
- **+**: Pause menu

### Menu Navigation
- **Left Stick/D-Pad**: Navigate menus
- **A**: Select/Confirm
- **B**: Back/Cancel
- **+**: Start/Enter
- **-**: Additional options

### Special Controls
- **+ and - together (hold 1 second)**: Exit game (return to Homebrew Launcher)

## Technical Details

### Platform Implementation
- **Graphics**: OpenGL ES 2.0 via EGL
- **Audio**: RetroArch-style audio system using audout API with multi-buffer streaming
- **Input**: libnx HID API for all controls and touch
- **Haptics**: HD Rumble with fade-out effects for weapon impacts and collisions
- **Memory**: Custom allocator optimized for Switch memory constraints
- **Storage**: Assets loaded from SD card

### Performance Considerations
- Dynamic resolution: 720p handheld / 1080p docked modes
- Targets 60 FPS with automatic resolution switching
- Optimized dock state detection (checked once per second)
- Memory usage kept under Switch homebrew limits
- Asset streaming to reduce memory footprint

### Known Limitations
- No gyroscope controls implemented
- Some advanced post-processing effects may be disabled
- Touch screen menu navigation not implemented

## Troubleshooting

### Build Issues
- Ensure Docker has sufficient memory allocated (at least 4GB recommended)
- Check that devkitPro packages are up to date in the Docker image
- Verify CMake toolchain file is properly configured

### Runtime Issues
- Verify all game assets are present and correctly named
- Check SD card has sufficient free space (at least 1GB recommended)
- Ensure Nintendo Switch has sufficient battery or is connected to power
- Try launching with minimal asset set (no music/video) if experiencing crashes

### Asset Problems
- Game assets must match the expected format (PSX data)
- Audio files should be in QOA format for music
- Texture files should be properly converted TIM/CMP format
- Track data must include all required files (.trf, .trs, .trv, etc.)

## Development Notes

### Architecture
The Switch port uses a custom platform layer (`platform_switch.c`) that interfaces with:
- libnx system services
- EGL for OpenGL ES context management  
- Switch-specific audio and input APIs
- SD card file system access

### Future Improvements
- Gyroscope steering support
- Touch screen menu navigation
- Additional controller support (GameCube adapter, etc.)

## Legal Notice

This is an unofficial community port. The original Wipeout game assets are required and not included. This port is intended for educational and preservation purposes only.

Ensure you own a legal copy of the original Wipeout game before using this port.