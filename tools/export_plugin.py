import shutil
import sys

from config_manager import config
from paths import ICONS_SOURCE_DIR, PROJECT_ROOT, get_gdextension_file_path
from scons_helpers import clear_screen


def package_plugin_release() -> None:
    plugin_name = config.getPluginName().lower()
    releases_dir = PROJECT_ROOT / "releases"

    # Staging structure paths
    staging_addons_dir = releases_dir / "addons"
    plugin_target_dir = staging_addons_dir / plugin_name

    print(f"Preparing Godot Asset Library release package for '{plugin_name}'...\n")

    # Handle releases directory setup (clean old files/zips)
    if releases_dir.exists():
        print("Cleaning previous releases directory contents...")
        try:
            shutil.rmtree(releases_dir)
        except Exception as e:
            print(f"Error: Could not clear previous releases folder: {e}", file=sys.stderr)
            sys.exit(1)

    releases_dir.mkdir(parents=True, exist_ok=True)

    # Create the exact plugin addon folder layout required by Godot Asset Store
    # Structure: releases/addons/<plugin_name>/
    plugin_target_dir.mkdir(parents=True, exist_ok=True)

    # Copy root bin/ folder into plugin directory
    root_bin_dir = PROJECT_ROOT / "bin"
    if root_bin_dir.exists() and root_bin_dir.is_dir():
        print("Copying compiled bin/ directory...")
        shutil.copytree(root_bin_dir, plugin_target_dir / "bin")
    else:
        print("Warning: Root bin/ directory not found! Make sure you compile the plugin before packaging.", file=sys.stderr)

    # Copy icons/ folder into plugin directory (if it exists)
    if ICONS_SOURCE_DIR.exists() and ICONS_SOURCE_DIR.is_dir():
        print("Copying icons/ directory...")
        shutil.copytree(ICONS_SOURCE_DIR, plugin_target_dir / "icons")

    # Copy the master .gdextension file from the project root into the plugin directory
    root_gdextension_path = get_gdextension_file_path()
    if root_gdextension_path.exists():
        print(f"Copying manifest file: {root_gdextension_path.name}")
        shutil.copy(root_gdextension_path, plugin_target_dir / root_gdextension_path.name)
    else:
        print(f"Error: Master .gdextension file not found at {root_gdextension_path}", file=sys.stderr)
        sys.exit(1)

    # Safely copy any README variant (README, README.md, README.txt, etc.) if it exists
    readme_copied = False
    for file_path in PROJECT_ROOT.iterdir():
        if file_path.is_file() and file_path.name.lower().startswith("readme"):
            dest_path = plugin_target_dir / file_path.name
            print(f"Copying documentation: {file_path.name}")
            shutil.copy(file_path, dest_path)
            readme_copied = True
            break
    if not readme_copied:
        print("Note: No README file found in project root (skipping).")

    # Safely copy any LICENSE variant (LICENSE, LICENSE.md, LICENSE.txt, etc.) if it exists
    license_copied = False
    for file_path in PROJECT_ROOT.iterdir():
        if file_path.is_file() and file_path.name.lower().startswith("license"):
            dest_path = plugin_target_dir / file_path.name
            print(f"Copying license: {file_path.name}")
            shutil.copy(file_path, dest_path)
            license_copied = True
            break
    if not license_copied:
        print("Note: No LICENSE file found in project root (skipping).")

    # Create the archive zip file named after the plugin inside releases/ root
    zip_output_base = releases_dir / plugin_name
    print(f"\nCompressing staging files into '{plugin_name}.zip'...")

    # shutil.make_archive creates <base_name>.zip using the directory specified under root_dir
    shutil.make_archive(
        base_name=str(zip_output_base),
        format="zip",
        root_dir=str(releases_dir),
        base_dir="addons"
    )

    # Clean up staging folders (addons/ layout) leaving only the final .zip archive
    print("Cleaning up temporary staging directories...")
    if staging_addons_dir.exists():
        shutil.rmtree(staging_addons_dir)

    print("\n" + "=" * 50)
    print("[SUCCESS] Release package created successfully!")
    print(f"Location: {releases_dir / f'{plugin_name}.zip'}")
    print("=" * 50)


if __name__ == "__main__":
    clear_screen()
    print("GDExtension Export Local Plugin Zip Tool by @realNikich\n")
    print("This utility bundles your local build binaries, icons, README, LICENSE, and .gdextension")
    print("manifest into a clean zip archive, ready for local distribution or deployment.\n")
    print("Tip: While GitHub Actions remains the best approach for multi-platform releases,")
    print("this tool gives you full local control.\n")
    print("Note: If you are on Windows, cross-compiling for Linux (via WSL), Web (via Emscripten),")
    print("and Android is entirely feasible with the proper toolchains installed.")
    print("(macOS and iOS targets will still require an Apple host or CI/CD workflow).\n")
    print("With the right setup, local packaging becomes a powerful workflow shortcut.")
    print("-" * 50 + "\n")
    choice = input("Press Enter to package release, or type 'q' to quit: ").strip().lower()
    if choice == 'q':
        print("Quitting...")
        sys.exit(0)

    package_plugin_release()
    input("\nPress Enter to continue...")
