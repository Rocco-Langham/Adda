#!/bin/sh
# Build and launch the Adda GUI.
set -e
cd "$(dirname "$0")"
bash build.sh
echo "launching adda-gui..."
if [ "$(uname -s)" = "Darwin" ]; then
    open Adda.app
else
    ./adda-gui.exe &
fi
