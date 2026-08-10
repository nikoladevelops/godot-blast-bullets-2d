import shutil
import sys

from paths import PROJECT_ROOT, get_plugin_dir
from scons_helpers import clear_screen, run_scons_clean


def clean_bin_directories() -> None:
    """
    Cleans the root bin directory and the target project's plugin bin directory if they exist.
    """
    print("\nCleaning local bin directories...")

    # Clean root bin directory
    root_bin = PROJECT_ROOT / "bin"
    if root_bin.exists() and root_bin.is_dir():
        try:
            shutil.rmtree(root_bin)
            print(f"Removed root bin directory: {root_bin}")
        except Exception as e:
            print(f"Warning: Could not remove {root_bin}: {e}", file=sys.stderr)

    # Clean targeted project's plugin bin directory
    plugin_dir = get_plugin_dir()
    plugin_bin = plugin_dir / "bin"
    if plugin_bin.exists() and plugin_bin.is_dir():
        try:
            shutil.rmtree(plugin_bin)
            print(f"Removed project plugin bin directory: {plugin_bin}")
        except Exception as e:
            print(f"Warning: Could not remove {plugin_bin}: {e}", file=sys.stderr)


if __name__ == "__main__":
    clear_screen()
    print("GDExtension SCons Clean Tool by @realNikich\n")
    print("This tool will run 'scons -c' and clean up all compiled bin directories.")
    print("-" * 50 + "\n")

    choice = input("Press Enter to proceed with cleaning, or type 'q' to quit: ").strip().lower()
    if choice == 'q':
        print("Quitting...")
        sys.exit(0)

    run_scons_clean()

    input("\nPress Enter to continue...")

    clean_bin_directories()
