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

def read_sconstruct_vars() -> Dict[str, bool]:
    content = read_file(SCONSTRUCT_PATH)
    vars = {}
    required_vars = ["is_2d_profile_used", "is_3d_profile_used", "is_custom_profile_used"]

    for var in required_vars:
        match = re.search(rf'^{var}\s*=\s*(True|False)\s*$', content, re.MULTILINE)
        if not match:
            print(f"Error: Variable '{var}' not found in {SCONSTRUCT_PATH}. Ensure it is defined as '{var} = True' or '{var} = False'.")
            exit(1)
        value = match.group(1).lower() == "true"
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

def update_sconstruct_vars(vars_to_set: Dict[str, bool]) -> None:
    content = read_file(SCONSTRUCT_PATH)
    new_content = content

    for var, value in vars_to_set.items():
        pattern = rf'^{var}\s*=\s*(True|False)\s*$'
        replacement = f'{var} = {str(value)}'
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
    return buckets, total_classes

def find_used_classes(source_dirs: List[str], include_dirs: List[str]) -> Set[str]:
    used = set()
    # Define additional directories to scan (excluding godot-cpp/gen)
    additional_dirs = [
        "godot-cpp/gdextension",
        "godot-cpp/include",
        "godot-cpp/src"
    ]
    dirs_to_scan = [os.path.join(PARENT_DIR, d) for d in source_dirs + include_dirs + additional_dirs]
    
    # Load all valid class names from the API
    api_content = read_file(API_JSON_PATH)
    api = json.loads(api_content)
    valid_classes = {cls.get("name") for cls in api.get("classes", [])}

    # Create filename_to_class map
    filename_to_class: Dict[str, str] = {}
    def pascal_to_snake(name: str) -> str:
        s1 = re.sub(r'(.)([A-Z][a-z]+)', r'\1_\2', name)
        s2 = re.sub(r'([a-z0-9])([A-Z])', r'\1_\2', s1)
        snake = s2.lower()
        snake = re.sub(r'([a-z])([0-9])', r'\1_\2', snake)
        snake = snake.replace('2_d', '2d').replace('3_d', '3d')
        return snake

    for cls in api.get("classes", []):
        name = cls.get("name")
        snake = pascal_to_snake(name)
        filename_to_class[snake] = name
        filename_to_class[name.lower()] = name  # Map lowercase class name directly
        # Explicitly handle OS and other critical classes
        if name in ['OS', 'ClassDB', 'Engine', 'ProjectSettings', 'Input', 'String', 'Variant']:
            filename_to_class[name.lower()] = name

    # Debug: Track scanned files and detected classes
    scanned_files = []
    detected_includes = []

    for dir_path in dirs_to_scan:
        if not os.path.exists(dir_path):
            continue
        for root, _, files in os.walk(dir_path):
            for file in files:
                if not file.endswith(('.cpp', '.h', '.hpp')):
                    continue
                path = os.path.join(root, file)
                scanned_files.append(path)
                content = read_file(path)
                
                # Check #include directives
                for line in content.splitlines():
                    if line.strip().startswith('#include'):
                        # Match classes in godot_cpp/(classes|core|variant)/
                        match = re.search(r'#include\s+["<](godot_cpp/(classes|core|variant)/([^/]+)\.h(pp)?)[">]', line)
                        if match:
                            include_path = match.group(1)
                            filename = match.group(3)
                            class_name = filename_to_class.get(filename)
                            if class_name and class_name in valid_classes:
                                used.add(class_name)
                                detected_includes.append((path, include_path, class_name))

                # Check for class names used directly in code
                for class_name in valid_classes:
                    # Look for class name as a whole word, case-insensitive
                    pattern = rf'\b{class_name}\b'
                    if re.search(pattern, content, re.IGNORECASE):
                        used.add(class_name)

    # Ensure critical utility classes are included if referenced
    critical_classes = {'OS', 'ClassDB', 'Engine', 'ProjectSettings', 'Input', 'String', 'Variant'}
    used.update(critical_classes & valid_classes & used)

    # Debug: Print scanned files and detected includes
    #print(f"Debug: Scanned {len(scanned_files)} files: {scanned_files}")
    #print(f"Debug: Detected includes: {detected_includes}")
    #print(f"Debug: Detected classes: {sorted(used)}")

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

def display_current_profile(vars: Dict[str, bool]) -> None:
    is_profile_used = any(vars[var] for var in vars)
    print(f"Build Profile Being Used: {str(is_profile_used).lower()}")
    print("Current Profile:")
    if not is_profile_used:
        print("  None (all classes included)")
    elif vars["is_2d_profile_used"]:
        print("  2D Profile")
    elif vars["is_3d_profile_used"]:
        print("  3D Profile")
    elif vars["is_custom_profile_used"]:
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

def handle_profile_choice(choice: str, extras: Dict[str, bool], buckets: Dict[str, Set[str]], total_classes: int) -> Tuple[Dict[str, bool], List[str], str]:
    new_vars = {
        "is_2d_profile_used": False,
        "is_3d_profile_used": False,
        "is_custom_profile_used": False
    }

    disabled_classes: List[str] = []
    profile_filename = ""

    source_dirs, include_dirs = read_sconstruct_dirs()

    if choice == "1":
        print("Profile set to None (all classes included).")
    elif choice == "2":
        new_vars["is_2d_profile_used"] = True
        disabled = set(buckets["3d"])
        for key, enabled in extras.items():
            if enabled:
                disabled |= buckets.get(key, set())
        disabled_classes = sorted(disabled)
        profile_filename = "2d_build_profile.json"
        generate_profile_json(profile_filename, disabled_classes)
    elif choice == "3":
        new_vars["is_3d_profile_used"] = True
        disabled = set(buckets["2d"])
        for key, enabled in extras.items():
            if enabled:
                disabled |= buckets.get(key, set())
        disabled_classes = sorted(disabled)
        profile_filename = "3d_build_profile.json"
        generate_profile_json(profile_filename, disabled_classes)
    elif choice == "4":
        new_vars["is_custom_profile_used"] = True
        profile_filename = "build_profile.json"
        profile_path = os.path.join(PARENT_DIR, profile_filename)
        auto_detect = input("\nShould I detect which classes you are using in your source files and header files and add them to your custom build profile automatically? (y/n): ").strip().lower() == "y"
        if auto_detect:
            print("This will take some time... please wait for generation to finish, don't close the window..")
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
                print("Warning: No Godot classes detected in your includes or code. Creating a default build_profile.json with minimal enabled classes.")
                profile = {
                    "_": "Default build profile with minimal enabled classes. Edit this file to specify additional 'enabled_classes' or use 'disabled_classes'.",
                    "type": "feature_profile",
                    "enabled_classes": ["Object", "RefCounted"]
                }
                write_file(profile_path, json.dumps(profile, indent=4))
                print(f"Default build_profile.json created at {profile_path} with enabled_classes: {profile['enabled_classes']}. Edit it to specify additional classes.")
            else:
                profile = {
                    "_": "Auto-generated custom build profile based on detected classes from #include directives and code usage (including inheritance dependencies).",
                    "type": "feature_profile",
                    "enabled_classes": sorted(all_needed)
                }
                write_file(profile_path, json.dumps(profile, indent=4))
                print("\n")
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

    if choice == "2":
        ensure_profile_exists("2d_build_profile.json", buckets)
    elif choice == "3":
        ensure_profile_exists("3d_build_profile.json", buckets)
    elif choice == "4":
        pass

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
        print("Custom User Profile enabled: You can edit build_profile.json to specify 'enabled_classes' or 'disabled_classes'.")

    print("SConstruct updated with new profile settings.")
    print("\nPlease recompile your project to apply the new build profile.")

    input("Press any key to continue...")

if __name__ == "__main__":
    main()