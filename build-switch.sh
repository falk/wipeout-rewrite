#!/bin/bash

# Nintendo Switch Build Script for Wipeout Rewrite
# This script builds the game using Docker with the devkitPro toolchain

set -e

echo "Building Wipeout Rewrite for Nintendo Switch..."

# Check if Docker is available
if ! command -v docker &> /dev/null; then
    echo "Error: Docker is required to build for Nintendo Switch"
    echo "Please install Docker and try again"
    exit 1
fi

# Check if docker-compose is available
if ! command -v docker-compose &> /dev/null; then
    echo "Error: docker-compose is required to build for Nintendo Switch"
    echo "Please install docker-compose and try again"
    exit 1
fi

# Create build directory
mkdir -p build-switch

# Build using Docker
echo "Starting Docker build environment..."
docker-compose -f docker-compose.switch.yml run --rm switch-builder bash -c "
    echo 'Setting up build environment...'
    cmake -S . -B build-switch \
        -DPLATFORM=SWITCH \
        -DRENDERER=GLES2 \
        -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake \
        -DCMAKE_BUILD_TYPE=Release

    echo 'Building Wipeout for Nintendo Switch...'
    cmake --build build-switch

    echo 'Build completed!'
    echo 'Output files:'
    ls -la build-switch/
"

echo ""
echo "Build completed successfully!"
echo "The .nro file should be in build-switch/"
echo ""
echo "To install on your Nintendo Switch:"
echo "1. Copy the .nro file to /switch/ on your SD card"
echo "2. Copy the wipeout assets to /switch/wipeout/ on your SD card"
echo "3. Launch via the Homebrew Launcher"