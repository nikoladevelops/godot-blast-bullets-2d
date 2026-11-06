#!/usr/bin/env bash
# Puts the built libraries, the .gdextension manifest and the icons into the
# test project (SConstruct skips this on CI runners, where CI is set).
set -euo pipefail
plugin="$(python3 -c "from tools.config_manager import config; print(config.getPluginName())")"
addon="test_project/addons/$plugin"
mkdir -p "$addon/bin/linux"
cp bin/linux/*.so "$addon/bin/linux/"
cp "$plugin.gdextension" "$addon/"
if [ -d icons ]; then
	rm -rf "$addon/icons"
	cp -R icons "$addon/icons"
fi
ls -la "$addon" "$addon/bin/linux"
