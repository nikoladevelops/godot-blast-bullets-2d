import json
import os
import re
import sys
from pathlib import Path

from paths import BUILD_PROFILES_DIR, PROJECT_ROOT, get_selected_extension_api_path
from scons_helpers import clear_screen, run_tool_script


def read_file(file_path: Path) -> str:
    try:
        return file_path.read_text(encoding="utf-8")
    except Exception as e:
        print(f"Error reading file {file_path}: {e}")
        sys.exit(1)


def write_file(file_path: Path, content: str) -> None:
    try:
        file_path.write_text(content, encoding="utf-8")
    except Exception as e:
        print(f"Error writing file {file_path}: {e}")
        sys.exit(1)


def _build_inheritance_map(api: dict) -> dict[str, str]:
    return {cls.get("name"): cls.get("inherits") for cls in api.get("classes", [])}


def _inherits_from(class_name: str, base_name: str, class_map: dict[str, str]) -> bool:
    seen = set()
    current = class_name
    while current and current not in seen:
        parent = class_map.get(current)
        if parent is None:
            return False
        if parent == base_name:
            return True
        seen.add(current)
        current = parent
    return False


def get_all_ancestors(class_name: str, class_map: dict[str, str]) -> set[str]:
    ancestors = set()
    current = class_map.get(class_name)
    seen = set()
    while current and current not in seen:
        ancestors.add(current)
        seen.add(current)
        current = class_map.get(current)
    return ancestors


def classify_api(api_path: Path) -> tuple[dict[str, set[str]], int]:
    content = read_file(api_path)
    api = json.loads(content)
    class_map = _build_inheritance_map(api)

    buckets: dict[str, set[str]] = {
        "2d": set(),
        "3d": set(),
        "xr": set(),
        "networking": set(),
        "navigation": set(),
        "editor": set(),
        "animation": set(),
        "ui": set(),
    }

    net_keywords = ("network", "http", "websocket", "multiplayer", "udp", "tcp", "packetpeer", "webrtc")

    for cls in api.get("classes", []):
        name = cls.get("name", "")
        lname = name.lower()

        if lname.endswith("2d") or _inherits_from(name, "Node2D", class_map):
            buckets["2d"].add(name)
        if lname.endswith("3d") or _inherits_from(name, "Node3D", class_map):
            buckets["3d"].add(name)
        if name.startswith("XR") or name == "WebXRInterface":
            buckets["xr"].add(name)
        if any(k in lname for k in net_keywords):
            buckets["networking"].add(name)
        if "navigation" in lname:
            buckets["navigation"].add(name)
        if _inherits_from(name, "EditorPlugin", class_map) or "editor" in lname:
            buckets["editor"].add(name)
        if "animation" in lname or _inherits_from(name, "AnimationPlayer", class_map) or _inherits_from(name, "AnimationMixer", class_map) or _inherits_from(name, "AnimationTree", class_map):
            buckets["animation"].add(name)
        if _inherits_from(name, "Control", class_map):
            buckets["ui"].add(name)

    total_classes = len(api.get("classes", []))
    return buckets, total_classes


def find_used_classes(api_path: Path) -> set[str]:
    used = set()

    # Scan project C++ source files and godot-cpp headers
    dirs_to_scan = [
        PROJECT_ROOT / "src",
        PROJECT_ROOT / "godot-cpp" / "gdextension",
        PROJECT_ROOT / "godot-cpp" / "include",
        PROJECT_ROOT / "godot-cpp" / "src",
    ]

    api_content = read_file(api_path)
    api = json.loads(api_content)
    valid_classes = {cls.get("name") for cls in api.get("classes", [])}

    filename_to_class: dict[str, str] = {}

    def pascal_to_snake(name: str) -> str:
        s1 = re.sub(r'(.)([A-Z][a-z]+)', r'\1_\2', name)
        s2 = re.sub(r'([a-z0-9])([A-Z])', r'\1_\2', s1)
        snake = s2.lower().replace('2_d', '2d').replace('3_d', '3d')
        return snake

    for cls in api.get("classes", []):
        name = cls.get("name")
        snake = pascal_to_snake(name)
        filename_to_class[snake] = name
        filename_to_class[name.lower()] = name
        if name in ['OS', 'ClassDB', 'Engine', 'ProjectSettings', 'Input', 'String', 'Variant']:
            filename_to_class[name.lower()] = name

    # Pre-compile regex matchers for performance
    include_regex = re.compile(r'#include\s+["<](godot_cpp/(classes|core|variant)/([^/]+)\.h(pp)?)[">]')

    for dir_path in dirs_to_scan:
        if not dir_path.exists():
            continue
        for root, _, files in os.walk(dir_path):
            for file in files:
                if not file.endswith(('.cpp', '.h', '.hpp')):
                    continue
                path = Path(root) / file
                content = read_file(path)

                # Fast check for include directives
                for line in content.splitlines():
                    if line.strip().startswith('#include'):
                        match = include_regex.search(line)
                        if match:
                            filename = match.group(3)
                            class_name = filename_to_class.get(filename)
                            if class_name and class_name in valid_classes:
                                used.add(class_name)

                # Scan content for explicit class references
                for class_name in valid_classes:
                    if class_name in content:
                        used.add(class_name)

    critical_classes = {'OS', 'ClassDB', 'Engine', 'ProjectSettings', 'Input', 'String', 'Variant'}
    used.update(critical_classes & valid_classes)
    return used


def prompt_extra_disables() -> dict[str, bool]:
    extras: dict[str, bool] = {}
    print("\nAdditional Class Filtering Options:")
    extras["xr"] = input("Do you want to disable XR classes? (y/n): ").strip().lower() == "y"
    extras["networking"] = input("Do you want to disable Networking-related classes? (y/n): ").strip().lower() == "y"
    extras["navigation"] = input("Do you want to disable Navigation-related classes? (y/n): ").strip().lower() == "y"
    extras["editor"] = input("Do you want to disable Editor-only classes? (y/n): ").strip().lower() == "y"
    extras["animation"] = input("Do you want to disable Animation-related classes? (y/n): ").strip().lower() == "y"
    extras["ui"] = input("Do you want to disable UI (Control) classes? (y/n): ").strip().lower() == "y"
    return extras


def edit_custom_profile(profile_path: Path, api_path: Path):
    """Applies code auto-detection or creates a default template for any custom profile file."""
    auto_detect = input(f"\nShould I detect which classes you are using in your source files and header files for '{profile_path.name}'? (y/n): ").strip().lower() == "y"

    if auto_detect:
        print(f"\nScanning C++ source files and headers for '{profile_path.name}'... please wait...")
        api_content = read_file(api_path)
        api = json.loads(api_content)
        class_map = _build_inheritance_map(api)

        used_classes = find_used_classes(api_path)
        all_needed = used_classes.copy()

        for cls in list(used_classes):
            if cls in class_map:
                all_needed |= get_all_ancestors(cls, class_map)

        all_classes = set(class_map.keys())
        all_needed = sorted(all_needed & all_classes)

        if not all_needed:
            print(f"\nWarning: No Godot classes detected. Creating '{profile_path.name}' with minimal enabled classes.")
            profile = {
                "_": "Default build profile with minimal enabled classes.",
                "type": "feature_profile",
                "enabled_classes": ["Object", "RefCounted"],
            }
        else:
            profile = {
                "_": "Auto-generated custom build profile based on detected classes from code usage.",
                "type": "feature_profile",
                "enabled_classes": all_needed,
            }
            print(f"\nCustom Profile generated: {len(all_needed)} classes enabled (including inheritance dependencies).")
    else:
        profile = {
            "_": "Default custom build profile with minimal base classes. Edit this file to manually specify 'enabled_classes' or 'disabled_classes'.",
            "type": "feature_profile",
            "enabled_classes": ["Object", "RefCounted"],
        }
        print(f"\nCreated default custom '{profile_path.name}'.")
        print("Note: The build profile is currently minimal (only Object and RefCounted are enabled).")
        print("You should now open the file in your text editor and populate either 'enabled_classes' (whitelist) or 'disabled_classes' (blacklist) according to your project needs.")

    write_file(profile_path, json.dumps(profile, indent=4))


def select_new_build_profile():
    input("\nPress Enter to continue...")
    run_tool_script("select_build_profile.py")


def edit_build_profile():
    clear_screen()
    print("Edit Build Profile Tool by @realNikich\n")

    print("-" * 75)
    print("ABOUT BUILD PROFILES & BEST PRACTICES:")
    print("-" * 75)
    print("• compilation Speed:")
    print("  Build profiles dramatically speed up compile times by stripping out unused Godot")
    print("  classes from the binding headers. If a profile is inaccurate, you can edit it manually.")
    print()
    print("• Recommended Workflow:")
    print("  1. Early Development: Stick to standard '2D Profile' or '3D Profile'.")
    print("  2. Project Completion: Switch to a 'Custom Profile' (auto-detection). This generates")
    print("     the leanest possible binary and saves huge CI/CD build time in GitHub Actions.")
    print()
    print("• IntelliSense & Auto-Completion Warning:")
    print("  Do NOT use auto-detected Custom Profiles when starting a fresh project with no code.")
    print("  Because unused classes are stripped out, your IDE's IntelliSense will not see them,")
    print("  preventing you from auto-completing classes you haven't written yet.")
    print()
    print("• Target Version Warning:")
    print("  Whenever you switch your Godot target version, ALWAYS update/regenerate your build")
    print("  profiles! Godot frequently adds new classes or removes deprecated ones between releases.")
    print("-" * 75 + "\n")


    if not BUILD_PROFILES_DIR.exists():
        BUILD_PROFILES_DIR.mkdir(parents=True, exist_ok=True)

    api_path = get_selected_extension_api_path()
    if not api_path.exists():
        print(f"Error: Target Extension API JSON not found at {api_path}")
        print("Please set a valid Godot target version first.")
        input("\nPress Enter to exit...")
        sys.exit(1)

    print("By re-generating a build profile, you ensure that it works for the current Godot API version, while also keeping only the classes that you truly need")
    print(f"Active Extension API: {api_path.name}\n")

    print("Re-Generate Build Profile File:")
    print("  [1] 2D Build Profile (2d_build_profile.json)")
    print("  [2] 3D Build Profile (3d_build_profile.json)")
    print("  [3] Custom User Profiles...")
    print("  [q] Quit")

    choice = input("\nEnter choice (1-3 or q): ").strip().lower()

    if choice == "q":
        print("Operation cancelled.")
        sys.exit(0)

    buckets, total_classes = classify_api(api_path)

    if choice == "1":
        # 2D Build Profile (Disables 3D + extras)
        extras = prompt_extra_disables()
        disabled = set(buckets["3d"])
        for key, enabled in extras.items():
            if enabled:
                disabled |= buckets.get(key, set())

        disabled_classes = sorted(disabled)
        profile_path = BUILD_PROFILES_DIR / "2d_build_profile.json"

        profile = {
            "_": "Default 2D build profile based on active Godot target API. Edit to modify 'disabled_classes'.",
            "type": "feature_profile",
            "disabled_classes": disabled_classes,
        }
        write_file(profile_path, json.dumps(profile, indent=4))
        print(f"\nSuccessfully generated 2d_build_profile.json ({len(disabled_classes)} classes disabled out of {total_classes}).")
        select_new_build_profile()

    elif choice == "2":
        # 3D Build Profile (Disables 2D + extras)
        extras = prompt_extra_disables()
        disabled = set(buckets["2d"])
        for key, enabled in extras.items():
            if enabled:
                disabled |= buckets.get(key, set())

        disabled_classes = sorted(disabled)
        profile_path = BUILD_PROFILES_DIR / "3d_build_profile.json"

        profile = {
            "_": "Default 3D build profile based on active Godot target API. Edit to modify 'disabled_classes'.",
            "type": "feature_profile",
            "disabled_classes": disabled_classes,
        }
        write_file(profile_path, json.dumps(profile, indent=4))
        print(f"\nSuccessfully generated 3d_build_profile.json ({len(disabled_classes)} classes disabled out of {total_classes}).")
        select_new_build_profile()

    elif choice == "3":
        # Dynamic Custom Profiles menu
        custom_files = sorted(
            p for p in BUILD_PROFILES_DIR.glob("*.json")
            if p.name not in {"2d_build_profile.json", "3d_build_profile.json"}
        )

        print("\nCustom Profiles in build_profiles/:")
        for idx, pf in enumerate(custom_files, start=1):
            print(f"  [{idx}] {pf.name}")
        print(f"  [{len(custom_files) + 1}] Create New Custom Profile (.json)")

        cust_choice = input(f"\nSelect a file to edit (1-{len(custom_files) + 1}): ").strip()

        if cust_choice.isdigit():
            c_num = int(cust_choice)
            if 1 <= c_num <= len(custom_files):
                selected_profile = custom_files[c_num - 1]
                edit_custom_profile(selected_profile, api_path)
                select_new_build_profile()
            elif c_num == len(custom_files) + 1:
                new_name = input("Enter new profile filename (e.g., build_profile.json): ").strip()
                if not new_name:
                    print("Invalid filename.")
                    return
                if not new_name.endswith(".json"):
                    new_name += ".json"
                edit_custom_profile(BUILD_PROFILES_DIR / new_name, api_path)
                select_new_build_profile()
            else:
                print("Invalid choice.")
        else:
            print("Invalid input.")

    else:
        print("Invalid option selected.")
        input("\nPress Enter to continue...")


if __name__ == "__main__":
    try:
        edit_build_profile()
    except KeyboardInterrupt:
        print("\nOperation cancelled.")
        sys.exit(0)
