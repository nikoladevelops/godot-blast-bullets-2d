#!/bin/sh
exec flatpak run --filesystem="$(cd "$(dirname "$0")" && pwd)" --command=vectorcraft-cli ai.storyteller.vectorcraft "$@"
