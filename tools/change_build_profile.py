#!/usr/bin/env python3
import os
import re
import json
import subprocess
from pathlib import Path
from typing import List, Set, Tuple, Dict

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PARENT_DIR = os.path.normpath(os.path.join(SCRIPT_DIR, ".."))
SCONSTRUCT_PATH = os.path.join(PARENT_DIR, "SConstruct")
API_JSON_PATH = os.path.join(PARENT_DIR, "godot-cpp", "gdextension", "extension_api.json")

def read_file(file_path: str) -> str:
    try:
        with open(file_path, "r", encoding="utf-8") as f:
            return f.read()
    except FileNotFoundError:
        print(f"Error: {file_path} not found.")
        exit(1)

def write_file(file_path: str, content: str) -> None:
    try:
        with open(file_path, "w", encoding="utf-8") as f:
            f.write(content)
    except OSError:
        print(f"Error: Could not write to {file_path}.")
        exit(1)

def read_sconstruct_vars() -> Dict[str, str]:
    content = read_file(SCONSTRUCT_PATH)
    vars = {}
    required_vars = ["is_2d_profile_used", "is_3d_profile_used", "is_custom_profile_used"]

    for var in required_vars:
        match = re.search(rf'^{var}\s*=\s*"([^"]+)"\s*$', content, re.MULTILINE)
        if not match:
            print(f"Error: Variable '{var}' not found in {SCONSTRUCT_PATH}. Ensure it is defined as '{var} = \"true\"' or '{var} = \"false\"'.")
            exit(1)
        value = match.group(1).lower()
        if value not in {"true", "false"}:
            print(f"Error: Variable '{var}' has invalid value '{value}' in {SCONSTRUCT_PATH}. Must be 'true' or 'false'.")
            exit(1)
        vars[var] = value

    return vars

def read_sconstruct_dirs() -> Tuple[List[str], List[str]]:
    content = read_file(SCONSTRUCT_PATH)
    source_dirs = []
    include_dirs = []

    source_match = re.search(r"opts\.Add\('source_dirs',\s*'[^']*',\s*'([^']+)'\)", content)
    if source_match:
        source_dirs = source_match.group(1).split(',')
    else:
        print("Warning: 'source_dirs' not found in SConstruct. Using default 'src'.")
        source_dirs = ['src']

    include_match = re.search(r"opts\.Add\('include_dirs',\s*'[^']*',\s*'([^']+)'\)", content)
    if include_match:
        include_dirs = include_match.group(1).split(',')
    else:
        print("Warning: 'include_dirs' not found in SConstruct. Using default 'src'.")
        include_dirs = ['src']

    return [d.strip() for d in source_dirs], [d.strip() for d in include_dirs]

def update_sconstruct_vars(vars_to_set: Dict[str, str]) -> None:
    content = read_file(SCONSTRUCT_PATH)
    new_content = content

    for var, value in vars_to_set.items():
        pattern = rf'^{var}\s*=\s*"[^"]+"\s*$'
        replacement = f'{var} = "{value}"'
        if not re.search(pattern, new_content, re.MULTILINE):
            print(f"Error: Variable '{var}' not found in {SCONSTRUCT_PATH} for updating.")
            exit(1)
        new_content = re.sub(pattern, replacement, new_content, flags=re.MULTILINE)

    write_file(SCONSTRUCT_PATH, new_content)

def clean_build_files() -> None:
    try:
        subprocess.run(["scons", "-c"], check=True, cwd=PARENT_DIR)
        print("Old build files cleaned successfully.")
    except subprocess.CalledProcessError as e:
        print(f"Warning: Failed to run 'scons -c' to clean old build files: {e}. Continuing anyway.")

def ensure_profile_exists(profile_filename: str, buckets: Dict[str, Set[str]] = None) -> None:
    if not profile_filename:
        return
    profile_path = os.path.join(PARENT_DIR, profile_filename)
    if not os.path.exists(profile_path):
        print(f"Creating {profile_filename} as it is required for the selected profile.")
        if profile_filename == "2d_build_profile.json" and buckets:
            disabled = set(buckets["3d"])
            profile = {
                "_": "Default 2D build profile. Edit this file to modify 'disabled_classes'.",
                "type": "feature_profile",
                "disabled_classes": sorted(disabled)
            }
        elif profile_filename == "3d_build_profile.json" and buckets:
            disabled = set(buckets["2d"])
            profile = {
                "_": "Default 3D build profile. Edit this file to modify 'disabled_classes'.",
                "type": "feature_profile",
                "disabled_classes": sorted(disabled)
            }
        else:
            profile = {
                "_": "Default build profile with minimal enabled classes. Edit this file to specify additional 'enabled_classes' or use 'disabled_classes'.",
                "type": "feature_profile",
                "enabled_classes": ["Object", "RefCounted"]
            }
        write_file(profile_path, json.dumps(profile, indent=4))
        print(f"Created {profile_filename} at {profile_path}.")

def _build_inheritance_map(api: dict) -> Dict[str, str]:
    return {cls.get("name"): cls.get("inherits") for cls in api.get("classes", [])}

def _inherits_from(class_name: str, base_name: str, class_map: Dict[str, str]) -> bool:
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

def classify_api() -> Tuple[Dict[str, Set[str]], int]:
    content = read_file(API_JSON_PATH)
    api = json.loads(content)
    class_map = _build_inheritance_map(api)

    buckets: Dict[str, Set[str]] = {
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
    # Debug: Verify bucket contents
    # for key, classes in buckets.items():
    #     print(f"DEBUG: Bucket '{key}' contains {len(classes)} classes: {sorted(classes)}")
    # print(f"DEBUG: Total classes in extension_api.json: {total_classes}")

    return buckets, total_classes

def find_used_classes(source_dirs: List[str], include_dirs: List[str]) -> Set[str]:
    used = set()
    dirs_to_scan = [os.path.join(PARENT_DIR, d) for d in source_dirs + include_dirs]
    for dir_path in dirs_to_scan:
        if not os.path.exists(dir_path):
            continue
        for root, _, files in os.walk(dir_path):
            for file in files:
                if not file.endswith(('.cpp', '.h', '.hpp')):
                    continue
                path = os.path.join(root, file)
                content = read_file(path)
                for line in content.splitlines():
                    if not line.strip().startswith('#include'):
                        continue
                    match = re.search(r'#include\s+["<](godot_cpp/classes/([^/]+)\.h(pp)?)[">]', line)
                    if match:
                        filename = match.group(2)
                        if '_' in filename:
                            class_name = ''.join(word.capitalize() for word in filename.split('_'))
                        else:
                            class_name = filename.capitalize()
                        used.add(class_name)
    return used

def get_all_ancestors(class_name: str, class_map: Dict[str, str]) -> Set[str]:
    ancestors = set()
    current = class_map.get(class_name)
    seen = set()
    while current and current not in seen:
        ancestors.add(current)
        seen.add(current)
        current = class_map.get(current)
    return ancestors

def generate_profile_json(file_name: str, disabled_classes: List[str]) -> None:
    profile = {
        "_": "Auto-generated build profile. For Custom Profile, edit build_profile.json to add 'enabled_classes' or 'disabled_classes'.",
        "type": "feature_profile",
        "disabled_classes": sorted(disabled_classes)
    }
    write_file(os.path.join(PARENT_DIR, file_name), json.dumps(profile, indent=4))

def display_current_profile(vars: Dict[str, str]) -> None:
    is_profile_used = any(vars[var] == "true" for var in vars)
    print(f"Build Profile Being Used: {str(is_profile_used).lower()}")
    print("Current Profile:")
    if not is_profile_used:
        print("  None (all classes included)")
    elif vars["is_2d_profile_used"] == "true":
        print("  2D Profile")
    elif vars["is_3d_profile_used"] == "true":
        print("  3D Profile")
    elif vars["is_custom_profile_used"] == "true":
        print("  Custom User Profile")

def get_user_choice() -> Tuple[str, Dict[str, bool]]:
    print("\nSelect Build Profile:")
    print("  1. None (use all classes)")
    print("  2. 2D Profile (disable 3D classes)")
    print("  3. 3D Profile (disable 2D classes)")
    print("  4. Custom User Profile (edit build_profile.json manually)")
    print("  Press 'q' to exit")
    choice = input("Enter choice (1-4 or q): ").strip().lower()

    if choice == "q":
        return "q", {}
    if choice not in {"1", "2", "3", "4"}:
        print("Invalid choice. Please enter 1, 2, 3, 4, or q.")
        exit(1)

    extras: Dict[str, bool] = {}
    if choice in {"2", "3"}:
        extras["xr"] = input("\nDo you want to disable XR classes? (y/n): ").strip().lower() == "y"
        extras["networking"] = input("Do you want to disable Networking-related classes? (y/n): ").strip().lower() == "y"
        extras["navigation"] = input("Do you want to disable Navigation-related classes? (y/n): ").strip().lower() == "y"
        extras["editor"] = input("Do you want to disable Editor-only classes? (y/n): ").strip().lower() == "y"
        extras["animation"] = input("Do you want to disable Animation-related classes? (y/n): ").strip().lower() == "y"
        extras["ui"] = input("Do you want to disable UI (Control) classes? (y/n): ").strip().lower() == "y"

    return choice, extras

def handle_profile_choice(choice: str, extras: Dict[str, bool], buckets: Dict[str, Set[str]], total_classes: int) -> Tuple[Dict[str, str], List[str], str]:
    new_vars = {
        "is_2d_profile_used": "false",
        "is_3d_profile_used": "false",
        "is_custom_profile_used": "false"
    }

    disabled_classes: List[str] = []
    profile_filename = ""

    source_dirs, include_dirs = read_sconstruct_dirs()

    if choice == "1":
        print("Profile set to None (all classes included).")
    elif choice == "2":
        new_vars["is_2d_profile_used"] = "true"
        disabled = set(buckets["3d"])
        for key, enabled in extras.items():
            if enabled:
                disabled |= buckets.get(key, set())
        disabled_classes = sorted(disabled)
        profile_filename = "2d_build_profile.json"
        generate_profile_json(profile_filename, disabled_classes)
    elif choice == "3":
        new_vars["is_3d_profile_used"] = "true"
        disabled = set(buckets["2d"])
        for key, enabled in extras.items():
            if enabled:
                disabled |= buckets.get(key, set())
        disabled_classes = sorted(disabled)
        profile_filename = "3d_build_profile.json"
        generate_profile_json(profile_filename, disabled_classes)
    elif choice == "4":
        new_vars["is_custom_profile_used"] = "true"
        profile_filename = "build_profile.json"
        profile_path = os.path.join(PARENT_DIR, profile_filename)
        auto_detect = input("\nShould I detect which classes you are using in your source files and header files and add them to your custom build profile automatically? (y/n): ").strip().lower() == "y"
        if auto_detect:
            api_content = read_file(API_JSON_PATH)
            api = json.loads(api_content)
            class_map = _build_inheritance_map(api)
            used_classes = find_used_classes(source_dirs, include_dirs)
            all_needed = used_classes.copy()
            for cls in list(used_classes):
                if cls in class_map:
                    all_needed |= get_all_ancestors(cls, class_map)
            all_classes = set(class_map.keys())
            all_needed = all_needed & all_classes
            if not all_needed:
                print("Warning: No Godot classes detected in your includes. Creating a default build_profile.json with minimal enabled classes.")
                profile = {
                    "_": "Default build profile with minimal enabled classes. Edit this file to specify additional 'enabled_classes' or use 'disabled_classes'.",
                    "type": "feature_profile",
                    "enabled_classes": ["Object", "RefCounted"]
                }
                write_file(profile_path, json.dumps(profile, indent=4))
                print(f"Default build_profile.json created at {profile_path} with enabled_classes: {profile['enabled_classes']}. Edit it to specify additional classes.")
            else:
                profile = {
                    "_": "Auto-generated custom build profile based on detected classes from #include directives (including inheritance dependencies).",
                    "type": "feature_profile",
                    "enabled_classes": sorted(all_needed)
                }
                write_file(profile_path, json.dumps(profile, indent=4))
                print(f"Custom Profile auto-generated: {len(all_needed)} classes enabled (including dependencies).")
                print("Warning: If additional classes are needed (e.g., indirect uses), edit build_profile.json manually.")
        else:
            if not os.path.exists(profile_path):
                print(f"build_profile.json not found at {profile_path}. Creating a default one with minimal enabled classes.")
                profile = {
                    "_": "Default build profile with minimal enabled classes. Edit this file to specify additional 'enabled_classes' or use 'disabled_classes'.",
                    "type": "feature_profile",
                    "enabled_classes": ["Object", "RefCounted"]
                }
                write_file(profile_path, json.dumps(profile, indent=4))
                print(f"Default build_profile.json created at {profile_path} with enabled_classes: {profile['enabled_classes']}. Edit it to specify additional classes.")
            else:
                print("Custom User Profile enabled: Edit build_profile.json to specify 'enabled_classes' or 'disabled_classes'.")

    return new_vars, disabled_classes, profile_filename

def main():
    print("Configure Custom Build Profile Tool by @realNikich")

    vars = read_sconstruct_vars()
    display_current_profile(vars)

    buckets, total_classes = classify_api()

    choice, extras = get_user_choice()
    if choice == "q":
        print("Exiting without changes.")
        exit(0)

    new_vars, disabled_classes, profile_filename = handle_profile_choice(choice, extras, buckets, total_classes)

    # Only ensure the profile file for the selected choice
    if choice == "2":
        ensure_profile_exists("2d_build_profile.json", buckets)
    elif choice == "3":
        ensure_profile_exists("3d_build_profile.json", buckets)
    elif choice == "4":
        # build_profile.json is already created in handle_profile_choice for choice 4
        pass
    # For choice 1, no profile is needed, so skip ensure_profile_exists

    print("\nAll old object files will now be cleaned up, so you will have to recompile everything when this finishes.")
    clean_build_files()

    update_sconstruct_vars(new_vars)

    print("\n")
    if choice == "1":
        print("Profile set to None (all classes included).")
    elif choice == "2":
        print(f"2D Profile enabled: {len(disabled_classes)} classes disabled out of {total_classes} total classes.")
        print("Warning: If you need some of the 3D or filtered classes that were disabled, you can always edit the 2d_build_profile.json and save your changes.")
    elif choice == "3":
        print(f"3D Profile enabled: {len(disabled_classes)} classes disabled out of {total_classes} total classes.")
        print("Warning: If you need some of the 2D or filtered classes that were disabled, you can always edit the 3d_build_profile.json and save your changes.")
    elif choice == "4":
        print("Custom User Profile enabled: Edit build_profile.json to specify 'enabled_classes' or 'disabled_classes'.")

    print("SConstruct updated with new profile settings.")
    print("\nPlease recompile your project to apply the new build profile.")

    input("Press any key to continue...")

if __name__ == "__main__":
    main()