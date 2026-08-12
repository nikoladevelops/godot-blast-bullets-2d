import sys

from config_manager import config
from gdextension_file_helper import set_reloadable
from paths import get_gdextension_file_path
from scons_helpers import clear_screen


def display_info_and_recommendations():
    print("-" * 75)
    print("ABOUT GDEXTENSION HOT RELOADING (reloadable = true / false):")
    print("-" * 75)
    print("• What is Hot Reloading?")
    print("  When reloadable = true, Godot unloads and reloads your binary DLL/.so")
    print("  on-the-fly whenever SCons finishes compiling a new build, WITHOUT needing")
    print("  to restart the Godot Editor.")
    print()
    print("• Advantages of 'reloadable = true' (Development Mode):")
    print("  - Extremely fast iteration cycles: edit C++ code -> build -> see results immediately.")
    print("  - Keeps your scene tree open and preserves editor state while hacking.")
    print()
    print("• Disadvantages & Risks of Hot Reloading:")
    print("  - Crash Potential: If hot reloading occurs while an object defined by your C++")
    print("    extension is active in the editor, Godot MAY CRASH.")
    print("  - Static / Singleton State: Static variables inside C++ are reset on reload.")
    print("  - ClassDB Changes: Adding/removing member variables or binding new methods")
    print("    during hot reload sometimes requires an editor restart anyway.")
    print()
    print("• Recommendations:")
    print("  1. Turned ON (true):  Use during active daily C++ logic development.")
    print("  2. Turned OFF (false): Turn off before final testing, production release,")
    print("     or if you experience random editor crashes during re-compilation.")
    print("-" * 75 + "\n")


def toggle_reloadable():
    clear_screen()
    print("GDExtension Reloadable Switcher Tool by @realNikich\n")

    display_info_and_recommendations()

    current_status = config.getReloadable()
    manifest_path = get_gdextension_file_path()

    print(f"Target Manifest: {manifest_path.name}")
    print(f"Current Hot Reload Status: {'ENABLED (reloadable = true)' if current_status else 'DISABLED (reloadable = false)'}\n")

    print("Options:")
    print("  [1] Enable Hot Reloading (reloadable = true)")
    print("  [2] Disable Hot Reloading (reloadable = false)")
    print("  [q] Quit without changing")

    choice = input("\nSelect choice (1, 2, or q): ").strip().lower()

    if choice == "q":
        print("Operation cancelled.")
        sys.exit(0)

    if choice == "1":
        new_status = True
    elif choice == "2":
        new_status = False
    else:
        print("Invalid selection.")
        sys.exit(1)

    # Save to config.json (JSON native boolean) and .gdextension manifest (reloadable = true/false)
    config.setReloadable(new_status)
    try:
        set_reloadable(new_status)
        status_str = "ENABLED (reloadable = true)" if new_status else "DISABLED (reloadable = false)"
        print(f"\nSuccessfully set Hot Reloading to: {status_str}")
        print(f"Updated tools/config.json and {manifest_path.name}.")
    except Exception as e:
        print(f"\nError updating .gdextension file: {e}")

    input("\nPress Enter to continue...")


if __name__ == "__main__":
    try:
        toggle_reloadable()
    except KeyboardInterrupt:
        print("\nOperation cancelled.")
        sys.exit(0)
