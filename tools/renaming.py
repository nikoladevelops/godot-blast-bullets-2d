import os
import sys
import re
import shutil

# Paths relative to script
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PARENT_DIR = os.path.normpath(os.path.join(SCRIPT_DIR, ".."))
TEST_PROJECT_DIR = os.path.join(PARENT_DIR, "test_project")
SRC_DIR = os.path.join(PARENT_DIR, "src")

# Global state for rollback
renamed_paths = []  # tuples of (new_path, old_path)
file_backups = {}   # path -> original content

def sanitize_and_validate_filename(name: str) -> str | None:
    cleaned = re.sub(r"\s+", "_", name.strip())
    cleaned = re.sub(r"[^a-zA-Z0-9_]", "", cleaned)
    if not cleaned or cleaned[0].isdigit():
        return None
    reserved_names = {
        "CON", "PRN", "AUX", "NUL",
        *(f"COM{i}" for i in range(1, 10)),
        *(f"LPT{i}" for i in range(1, 10)),
    }
    if cleaned.upper() in reserved_names:
        return None
    if cleaned.endswith(".") or cleaned.endswith(" "):
        return None
    return cleaned.lower()

def get_old_plugin_name():
    file_path = os.path.join(PARENT_DIR, "dont_touch.txt")
    with open(file_path, "r") as f:
        lines = f.readlines()
    if not lines:
        raise ValueError("dont_touch.txt is empty.")
    return lines[0].strip().lower()

def verify_paths_exist(paths):
    missing = [p for p in paths if not os.path.exists(p)]
    if missing:
        for path in missing:
            print(f"Error: Required path does not exist: {path}", file=sys.stderr)
        sys.exit(1)

def backup_file(path):
    with open(path, "r", encoding="utf-8") as f:
        file_backups[path] = f.read()

def restore_file_contents():
    for path, content in file_backups.items():
        try:
            with open(path, "w", encoding="utf-8") as f:
                f.write(content)
        except Exception as e:
            print(f"Warning: Could not restore file {path}: {e}", file=sys.stderr)

def rename_path(old_path, new_path):
    os.rename(old_path, new_path)
    renamed_paths.append((new_path, old_path))

def rollback_renames():
    for new_path, old_path in reversed(renamed_paths):
        try:
            os.rename(new_path, old_path)
        except Exception as e:
            print(f"Warning: Could not rollback rename {new_path} -> {old_path}: {e}", file=sys.stderr)

def find_file_case_insensitive(directory, pattern):
    for filename in os.listdir(directory):
        if filename.lower() == pattern.lower():
            return os.path.join(directory, filename)
    return None

def delete_bin_folders(paths):
    for path in paths:
        if os.path.isdir(path):
            try:
                shutil.rmtree(path)
            except Exception as e:
                print(f"Warning: Could not delete folder {path}: {e}", file=sys.stderr)

def rename_and_track_paths(old_name, new_name):
    new_name_lower = new_name.lower()

    old_plugin_dir = os.path.join(TEST_PROJECT_DIR, old_name)
    new_plugin_dir = os.path.join(TEST_PROJECT_DIR, new_name_lower)
    rename_path(old_plugin_dir, new_plugin_dir)

    gdextension_pattern = f"{old_name}.gdextension"
    old_gdextension = find_file_case_insensitive(new_plugin_dir, gdextension_pattern)
    if old_gdextension is None:
        raise FileNotFoundError(f"Cannot find {gdextension_pattern} in {new_plugin_dir}")
    new_gdextension = os.path.join(new_plugin_dir, f"{new_name_lower}.gdextension")
    rename_path(old_gdextension, new_gdextension)

    uid_pattern = f"{old_name}.gdextension.uid"
    old_uid = find_file_case_insensitive(new_plugin_dir, uid_pattern)
    if old_uid is not None:
        new_uid = os.path.join(new_plugin_dir, f"{new_name_lower}.gdextension.uid")
        rename_path(old_uid, new_uid)

    return {
        "plugin_dir": new_plugin_dir,
        "gdextension": new_gdextension,
        "register_types": os.path.join(SRC_DIR, "register_types.cpp"),
        "sconstruct": os.path.join(PARENT_DIR, "SConstruct")
    }

def edit_file_with_subs(path, subs):
    backup_file(path)
    with open(path, "r", encoding="utf-8") as f:
        content = f.read()
    for pattern, repl, flags in subs:
        content = re.sub(pattern, repl, content, flags=flags)
    with open(path, "w", encoding="utf-8") as f:
        f.write(content)

def edit_gdextension(path, old_name, new_name):
    backup_file(path)
    with open(path, "r", encoding="utf-8") as f:
        content = f.read()

    content = re.sub(
        r'entry_symbol\s*=\s*"[^"]*_init"',
        f'entry_symbol = "{new_name.lower()}_init"',
        content,
        flags=re.IGNORECASE
    )

    lines = content.splitlines(keepends=True)
    updated_lines = []
    in_libraries = False

    lib_pattern = re.compile(rf'lib{re.escape(old_name)}\.', flags=re.IGNORECASE)
    path_pattern = re.compile(rf'/{re.escape(old_name)}\.', flags=re.IGNORECASE)

    for line in lines:
        if line.strip().startswith("[libraries]"):
            in_libraries = True
        if in_libraries:
            line = lib_pattern.sub(f'lib{new_name.lower()}.', line)
            line = path_pattern.sub(f'/{new_name.lower()}.', line)
        updated_lines.append(line)

    with open(path, "w", encoding="utf-8") as f:
        f.writelines(updated_lines)

def edit_register_types(path, new_name):
    subs = [
        (
            r'(GDExtensionBool GDE_EXPORT )\w+(_init\s*\()',
            r'\1' + new_name.lower() + r'\2',
            re.IGNORECASE
        )
    ]
    edit_file_with_subs(path, subs)

def edit_sconstruct(path, new_name):
    subs = [
        (
            r'libname\s*=\s*"[^"]+"',
            f'libname = "{new_name.lower()}"',
            0
        )
    ]
    edit_file_with_subs(path, subs)

def update_dont_touch(new_name):
    path = os.path.join(PARENT_DIR, "dont_touch.txt")
    with open(path, "r", encoding="utf-8") as f:
        lines = f.readlines()
    lines[0] = new_name + "\n"
    with open(path, "w", encoding="utf-8") as f:
        f.writelines(lines)

def edit_github_yml(path, old_name, new_name):
    backup_file(path)
    with open(path, "r", encoding="utf-8") as f:
        content = f.read()

    # Replace the PLUGIN_NAME line
    content = re.sub(
        r'(PLUGIN_NAME:\s*")[^"]+(")',
        rf'\1{new_name.lower()}\2',
        content
    )

    with open(path, "w", encoding="utf-8") as f:
        f.write(content)

def update_plugin_name(new_name):
    old_name = get_old_plugin_name()

    old_plugin_dir = os.path.join(TEST_PROJECT_DIR, old_name)
    old_gdextension = os.path.join(old_plugin_dir, f"{old_name}.gdextension")
    register_types_path = os.path.join(SRC_DIR, "register_types.cpp")
    sconstruct_path = os.path.join(PARENT_DIR, "SConstruct")

    verify_paths_exist([
        old_plugin_dir,
        old_gdextension,
        register_types_path,
        sconstruct_path
    ])

    try:
        paths = rename_and_track_paths(old_name, new_name)

        delete_bin_folders([
            os.path.join(PARENT_DIR, "bin"),
            os.path.join(paths["plugin_dir"], "bin")
        ])

        edit_gdextension(paths["gdextension"], old_name, new_name)
        edit_register_types(paths["register_types"], new_name)
        edit_sconstruct(paths["sconstruct"], new_name)
        update_dont_touch(new_name)

        # Edit the build-plugin.yml file
        build_debug_path = os.path.join(PARENT_DIR, ".github", "workflows", "build-plugin.yml")
        if os.path.exists(build_debug_path):
            edit_github_yml(build_debug_path, old_name, new_name)

        print("\nPlugin renamed successfully.\nPlease recompile the plugin to apply changes.\n")
        input("Press any key to continue...")

    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        print("Rolling back changes...", file=sys.stderr)
        restore_file_contents()
        rollback_renames()
        print("Rollback complete.", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    if len(sys.argv) < 1:
        print("Usage: python renaming.py", file=sys.stderr)
        sys.exit(1)
    
    print("Tool For Renaming Your GDExtension Plugin By @realNikich\n")
    print("Warning: Your plugin name will always be lowercase when used as a file or directory name. This follows the correct naming convention in Godot, so do NOT change it.")
    print("Warning: Before renaming, make sure your test project is closed and the game is not running.")
    print("Warning: After renaming, you need to recompile your code because the bin folders will be deleted.")
    print("\n")

    new_plugin_name = input("Please enter your plugin name or type 'q' to quit: ").strip()

    if new_plugin_name.lower() == 'q':
        print("Quitting...")
        sys.exit(0)

    sanitized = sanitize_and_validate_filename(new_plugin_name)

    if not sanitized:
        print("Error: Invalid plugin name. Please use a valid identifier.", file=sys.stderr)
        sys.exit(1)
    else:
        update_plugin_name(sanitized)
