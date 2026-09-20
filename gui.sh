#!/bin/sh
# Build and launch the Adda GUI.
set -e
cd "$(dirname "$0")"
bash build.sh
echo "launching adda-gui..."
./adda-gui.exe &
