import json
import sys

from config_manager import config
from gdextension_file_helper import set_compatibility_minimum
from paths import GDEXTENSION_APIS_PATH, PROJECT_ROOT
from scons_helpers import run_scons_clean, run_tool_script


def discover_extension_apis():
    """
    Scans GDEXTENSION_APIS_PATH for files starting with 'extension_api' and ending in '.json'.
    Reads each file to extract 'version_major' and 'version_minor'.

    Returns a sorted list of dictionaries containing version info and file paths.
    """
    if not GDEXTENSION_APIS_PATH.exists():
        print(f"Error: Path does not exist: {GDEXTENSION_APIS_PATH}")
        sys.exit(1)

    available_versions = []

    # Find all extension_api*.json files inside the directory
    for file_path in GDEXTENSION_APIS_PATH.glob("extension_api*.json"):
        try:
            with open(file_path, "r", encoding="utf-8") as f:
                data = json.load(f)

            # Extract header version data safely
            header = data.get("header", {})
            major = header.get("version_major")
            minor = header.get("version_minor")

            # Fallback: Check root dict if not nested inside 'header'
            if major is None:
                major = data.get("version_major")
                minor = data.get("version_minor")

            if major is not None and minor is not None:
                version_str = f"{major}.{minor}"

                # Compute path relative to PROJECT_ROOT
                try:
                    relative_path = file_path.relative_to(PROJECT_ROOT).as_posix()
                except ValueError:
                    # Fallback to as_posix string if not under PROJECT_ROOT
                    relative_path = file_path.as_posix()

                available_versions.append({
                    "version": version_str,
                    "major": major,
                    "minor": minor,
                    "file_name": file_path.name,
                    "path": file_path,
                    "relative_path": relative_path
                })
        except (json.JSONDecodeError, OSError) as e:
            print(f"Warning: Failed to read {file_path.name}: {e}")

    # Sort versions
    available_versions.sort(key=lambda x: (x["major"], x["minor"]), reverse=True)
    return available_versions


def select_godot_target_version():
    print("Tool For Switching Godot Target Version By @realNikich\n")

    versions = discover_extension_apis()

    if not versions:
        print(f"No valid extension_api JSON files found in {GDEXTENSION_APIS_PATH}")
        sys.exit(1)

    current_config_version = config.getGodotVersion()
    print(f"Current Godot Target Version: {current_config_version}\n")
    print("Available API versions found:")

    for idx, item in enumerate(versions, start=1):
        is_current = " (Current)" if item["version"] == current_config_version else ""
        print(f"  [{idx}] Godot {item['version']} ({item['file_name']}){is_current}")

    print("  [q] Quit without changing")
    print("\nWarning: After changing the Godot target version, you need to recompile your code for the changes to take effect.\n")

    while True:
        user_input = input("\nSelect a version number or 'q' to quit: ").strip().lower()

        if user_input == "q":
            print("Operation cancelled. Exiting...")
            sys.exit(0)

        if user_input.isdigit():
            choice_num = int(user_input)
            if 1 <= choice_num <= len(versions):
                selected = versions[choice_num - 1]

                # Save version and relative API path in config.json
                config.setGodotVersion(selected["version"])
                config.setExtensionApiPath(selected["relative_path"])

                # Update .gdextension compatibility minimum
                set_compatibility_minimum(selected["version"])

                # Clear all old cache
                run_scons_clean()

                # Run edit build profile script (since a new godot target version was selected the Godot API will be different)
                run_tool_script("edit_build_profile.py")

                print(f"\nSuccessfully updated to Godot Version {selected['version']}!")
                print(f"Active API Path: {selected['relative_path']}")
                print("Please recompile for changes to take effect..")
                input("\nPress Enter to continue...")
                break;
        else:
            print(f"Invalid selection! Please enter a number between 1 and {len(versions)}, or 'q'.")


if __name__ == "__main__":
    try:
        select_godot_target_version()
    except KeyboardInterrupt:
        print("\nOperation cancelled.")
        sys.exit(0)
