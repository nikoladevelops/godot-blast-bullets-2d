#!/usr/bin/env bash
# Downloads the Godot editor ($GODOT_VERSION, Linux x86_64) and exports
# GODOT_BIN for the next steps (tools/run_tests.py reads it).
set -euo pipefail
name="Godot_v${GODOT_VERSION}-stable_linux.x86_64"
dir="${RUNNER_TEMP:-/tmp}/godot"
mkdir -p "$dir"
curl -fsSL -o "$dir/godot.zip" "https://github.com/godotengine/godot/releases/download/${GODOT_VERSION}-stable/${name}.zip"
unzip -q -o "$dir/godot.zip" -d "$dir"
chmod +x "$dir/$name"
"$dir/$name" --version
echo "GODOT_BIN=$dir/$name" >> "$GITHUB_ENV"
