#!/bin/bash
set -e

echo "Installing Harmony Hub..."

if [ -z "$PREFIX" ]; then
    if [ "$EUID" -ne 0 ]; then
        PREFIX="$HOME/.local"
        echo "Running as standard user. Installing to $PREFIX"
    else
        PREFIX="/usr/local"
        echo "Running as root. Installing to $PREFIX"
    fi
fi

BIN_DIR="$PREFIX/bin"

echo "Step 1: Creating directories in $PREFIX..."
mkdir -p "$BIN_DIR"

echo "Step 2: Building Harmony Hub..."
make clean
make -j$(nproc)

echo "Step 3: Installing binary..."
cp harmony_hub "$BIN_DIR/harmony_hub"
chmod 755 "$BIN_DIR/harmony_hub"

if [ -f "hub_gui" ]; then
    cp hub_gui "$BIN_DIR/hub_gui"
    chmod 755 "$BIN_DIR/hub_gui"
fi

echo "----------------------------------------"
echo "Installation complete!"
echo "Harmony Hub is now installed to $BIN_DIR."
echo "You can start it by running 'harmony_hub'."
echo "IMPORTANT: If you installed to ~/.local/bin, make sure ~/.local/bin is in your PATH."
