#!/bin/bash
# exit on any error
set -e 

echo "========================================="
echo "DOSTCG - Ultimate Build System (Linux)"
echo "========================================="

# Ensure the WATCOM environment variable is set
if [ -z "$WATCOM" ]; then
    echo "Error: WATCOM environment variable is not set."
    echo "Please point it to your Open Watcom installation (e.g., /opt/watcom)."
    exit 1
fi

# Set up Watcom paths for Linux
export INCLUDE="$WATCOM/h"
# Add both 64-bit and 32-bit Watcom binary paths
export PATH="$WATCOM/binl64:$WATCOM/binl:$PATH"

echo "Cleaning old files..."
rm -f *.obj game.exe

echo "Compiling C code..."
wcc main.c -mm -I"$WATCOM/h" -fo=main.obj
wcc vga.c -mm -I"$WATCOM/h" -fo=vga.obj
wcc serial.c -mm -I"$WATCOM/h" -fo=serial.obj

echo "Linking game.exe..."
wlink system dos name game.exe file main.obj file vga.obj file serial.obj libpath "$WATCOM/lib286/dos" libpath "$WATCOM/lib286" library clibm.lib option stack=8192

if [ -f "game.exe" ]; then
    echo ""
    echo "Build successful! game.exe created."
else
    echo ""
    echo "Build failed! game.exe not found."
    exit 1
fi