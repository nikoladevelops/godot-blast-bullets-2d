# GDExtension Setup and Template Manager by @realNikich
import sys

from tools.config_manager import config
from tools.scons_helpers import clear_screen, run_tool_script

SUBMENUS = {
    "1": {
        "title": "Godot Engine & Version Settings",
        "items": [
            ("Change Godot Target Version", "change_godot_target_version.py"),
            ("Select Godot Engine Executable Path", "select_godot_path.py"),
            ("Update godot_cpp Submodule To Latest", "update_godot_cpp.py"),
            ("Select Godot Project Folder", "select_godot_project.py"),
        ],
    },
    "2": {
        "title": "Plugin Configuration & Assets",
        "items": [
            ("Rename Plugin", "renaming.py"),
            ("Update Editor Node Icons", "update_icons.py"),
            ("Generate Missing XML Documentation Files", "generate_xml_docs.py"),
        ],
    },
    "3": {
        "title": "Compilation & Build Settings",
        "items": [
            ("Compile Plugin Debug Build", "compile_debug_build.py"),
            ("Compile Plugin Release Build", "compile_release_build.py"),
            ("Select Build Profile", "select_build_profile.py"),
            ("Edit Build Profile", "edit_build_profile.py"),
            ("Change LTO Mode For Release Builds", "change_lto_mode.py"),
            ("Toggle Editor Build Target", "toggle_editor_target.py"),
            ("Toggle Debug Symbols (Allows Attaching Debugger)", "toggle_debug_symbols.py"),
            ("Clean Compiled SCons Build Artifacts", "clean_build.py"),
        ],
    },
    "4": {
        "title": "Advanced & Workflow Options",
        "items": [
            ("Hot Reloading Options", "toggle_reloadable.py"),
            ("Export Local Plugin Zip Release (Ready For Godot Asset Store)", "export_plugin.py"),
            ("Tutorials And Help With Godot C++", "tutorials.py"),
        ],
    },
}

def get_active_path_version() -> str:
    """Helper to find the version tag of the currently active Godot path."""
    active_path = config.getGodotActivePath()
    if not active_path:
        return "None Set"

    saved_entries = config.getSavedGodotPaths()
    for entry in saved_entries:
        if entry.get("path") == active_path:
            return entry.get("version", "Unknown")

    return "Unknown"


def display_dashboard_header():
    """Display comprehensive status header showing all configuration fields from config.json."""
    plugin_name = config.getPluginName()
    godot_version = config.getGodotVersion()
    godot_path = config.getGodotActivePath()
    project_folder = config.getGodotProjectFolder()
    build_profile = config.getSelectedBuildProfile()
    extension_api_path = config.getExtensionApiPath()
    lto_mode = config.getLtoMode()
    reloadable = config.getReloadable()
    editor_target_mode = config.getEditorTargetMode()
    debug_symbols = config.getDebugSymbols()
    active_version = get_active_path_version()
    saved_paths = config.getSavedGodotPaths()

    print("=" * 65)
    print(f" Godot++ Template Manager by @realNikich | Plugin: {plugin_name}")
    print("=" * 65)
    print(f"- Target Godot Version: {godot_version}")
    print(f"- Active Godot Path: {godot_path or 'NONE'} ({active_version})")
    print(f"- Saved Godot Paths Count: {len(saved_paths)}")
    print(f"- Godot Project Folder: {project_folder or 'NONE'}")
    print(f"- Selected Build Profile: {build_profile or 'NONE'}")
    print(f"- Extension API Path: {extension_api_path or 'NONE'}")
    print(f"- LTO Mode: {lto_mode}")
    print(f"- Editor Target Mode: {editor_target_mode}")
    print(f"- Hot Reload Status (reloadable): {'Enabled' if reloadable else 'Disabled'}")
    print(f"- Debug Symbols: {debug_symbols.upper()}")
    print("=" * 65 + "\n")


def run_quick_setup_wizard():
    """Walks the user through the standard first-time setup sequence step-by-step."""
    clear_screen()
    print("=" * 65)
    print(" Godot++ Quick Setup Wizard")
    print("=" * 65)
    print("This wizard will guide you through setting up your project for the first time:")
    print("  1. Update godot-cpp submodule")
    print("  2. Set your target Godot version, along with new build profile")
    print("  3. Select your Godot Engine Executable Path")
    print("  4. Select your Godot Project (Game) Folder")
    print("  5. Rename your plugin")
    print("  6. Compile your first Debug build")
    print("-" * 65)

    proceed = input("Do you want to start the quick setup wizard now? (y/q to quit): ").strip().lower()
    if proceed == 'q':
        return

    steps = [
        ("Updating godot-cpp submodule...", "update_godot_cpp.py"),
        ("Setting Godot Target Version...", "change_godot_target_version.py"),
        ("Selecting Godot Engine Executable Path...", "select_godot_path.py"),
        ("Selecting Godot Project Folder...", "select_godot_project.py"),
        ("Renaming Plugin...", "renaming.py"),
        ("Compiling Initial Debug Build...", "compile_debug_build.py"),
    ]

    for desc, script in steps:
        clear_screen()
        print(f"[Quick Setup Wizard] -> {desc}\n" + "-" * 50)
        run_tool_script(script)

    clear_screen()
    print("=" * 65)
    print(" Quick Setup Wizard Completed Successfully!")
    print("Your C++ GDExtension template is fully configured and ready for development.")
    print("=" * 65)
    input("Press Enter to return to the main menu...")


def display_main_menu():
    """Display the main submenu categories and quick setup option."""
    display_dashboard_header()
    print("Main Menu Categories:")
    print("  [0] Run Quick Setup Wizard (First-Time Users)")
    for key, submenu in SUBMENUS.items():
        print(f"  [{key}] {submenu['title']}")
    print("\nEnter category number, or 'q' to quit: ")


def display_submenu(category_key):
    """Display options inside a specific submenu category."""
    submenu = SUBMENUS[category_key]
    while True:
        clear_screen()
        display_dashboard_header()
        print(f"Submenu: {submenu['title']}")
        print("-" * 50)

        items = submenu["items"]
        for idx, (title, _) in enumerate(items, start=1):
            print(f"  {idx}. {title}")

        print("  q. Back to Main Menu")
        print("-" * 50)
        user_input = input("Select an option: ").strip().lower()

        if user_input == "q":
            break

        if user_input.isdigit() and 1 <= int(user_input) <= len(items):
            _, script_filename = items[int(user_input) - 1]
            run_tool_script(script_filename)
        else:
            print("Invalid choice. Please enter a valid number or 'q' to go back.")
            input("Press Enter to continue...")


def main():
    """Main loop to display categories and handle user input."""
    while True:
        config.reload()
        clear_screen()
        display_main_menu()
        user_input = input().strip().lower()

        if user_input == "q":
            print("Quitting...")
            sys.exit(0)

        if user_input == "0":
            run_quick_setup_wizard()
        elif user_input in SUBMENUS:
            display_submenu(user_input)
        else:
            print("Invalid category selection. Please try again.")
            input("Press Enter to continue...")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nQuitting...")
        sys.exit(0)
