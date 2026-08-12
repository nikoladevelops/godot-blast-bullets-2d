import os
import re
import shutil
import sys

from config_manager import config
from gdextension_file_helper import (
    purge_old_project_gdextension,
    sync_gdextension_to_target_project,
)
from paths import PROJECT_ROOT, SRC_DIR, get_godot_project_dir

# Global state for rollback tracking
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


def delete_bin_folders(paths):
    for path in paths:
        if os.path.isdir(path):
            try:
                shutil.rmtree(path)
            except Exception as e:
                print(f"Warning: Could not delete folder {path}: {e}", file=sys.stderr)


def edit_file_with_subs(path, subs):
    backup_file(path)
    with open(path, "r", encoding="utf-8") as f:
        content = f.read()
    for pattern, repl, flags in subs:
        content = re.sub(pattern, repl, content, flags=flags)
    with open(path, "w", encoding="utf-8") as f:
        f.write(content)


def edit_register_types(path, new_name):
    subs = [
        (
            r'(GDExtensionBool GDE_EXPORT )\w+(_init\s*\()',
            r'\1' + new_name.lower() + r'\2',
            re.IGNORECASE
        )
    ]
    edit_file_with_subs(path, subs)


def edit_gdextension_contents(path, old_name, new_name):
    """Safely updates entry symbols and internal library/dependency path references inside the root .gdextension file."""
    backup_file(path)
    with open(path, "r", encoding="utf-8") as f:
        content = f.read()

    # Update entry symbol
    content = re.sub(
        r'entry_symbol\s*=\s*"[^"]*_init"',
        f'entry_symbol = "{new_name.lower()}_init"',
        content,
        flags=re.IGNORECASE
    )

    # Update library file/path references containing the old plugin name
    lines = content.splitlines(keepends=True)
    updated_lines = []
    in_target_section = False

    lib_pattern = re.compile(rf'lib{re.escape(old_name)}\.', flags=re.IGNORECASE)
    path_pattern = re.compile(rf'/{re.escape(old_name)}\.', flags=re.IGNORECASE)
    dll_pattern = re.compile(rf'\b{re.escape(old_name)}\.', flags=re.IGNORECASE)

    for line in lines:
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            in_target_section = stripped.lower() in ["[libraries]", "[dependencies]", "[icons]"]

        if in_target_section:
            line = lib_pattern.sub(f'lib{new_name.lower()}.', line)
            line = path_pattern.sub(f'/{new_name.lower()}.', line)
            line = dll_pattern.sub(f'{new_name.lower()}.', line)

        updated_lines.append(line)

    with open(path, "w", encoding="utf-8") as f:
        f.writelines(updated_lines)


def update_plugin_name(new_name):
    old_name = config.getPluginName().lower()
    new_name_lower = new_name.lower()

    if old_name == new_name_lower:
        print("Error: The new plugin name is identical to the current one.", file=sys.stderr)
        sys.exit(1)

    project_dir = get_godot_project_dir()
    addons_dir = project_dir / "addons"

    old_plugin_dir = addons_dir / old_name
    new_plugin_dir = addons_dir / new_name_lower

    root_old_gdextension = PROJECT_ROOT / f"{old_name}.gdextension"
    root_new_gdextension = PROJECT_ROOT / f"{new_name_lower}.gdextension"
    register_types_path = SRC_DIR / "register_types.cpp"

    verify_paths_exist([
        str(old_plugin_dir),
        str(root_old_gdextension),
        str(register_types_path)
    ])

    try:
        # Purge stale/locked manifest files from the target test project first
        purge_old_project_gdextension(old_name)

        # Rename plugin folder inside project addons (if it exists)
        if old_plugin_dir.exists():
            rename_path(str(old_plugin_dir), str(new_plugin_dir))

        # Rename root workspace .gdextension manifest
        rename_path(str(root_old_gdextension), str(root_new_gdextension))

        # Clean compiled binaries directories
        delete_bin_folders([
            str(PROJECT_ROOT / "bin"),
            str(new_plugin_dir / "bin") if new_plugin_dir.exists() else ""
        ])

        # Update C++ source entry points, root gdextension contents, and configuration state
        edit_register_types(str(register_types_path), new_name_lower)
        edit_gdextension_contents(str(root_new_gdextension), old_name, new_name_lower)
        config.setPluginName(new_name_lower)

        # Re-sync the newly updated root manifest to the test project cleanly
        sync_gdextension_to_target_project()

        print("\nPlugin renamed successfully via config.json!")
        print("Root .gdextension contents updated, stale test project manifests purged, and clean template resynced.")
        print("Please recompile the plugin to generate fresh binaries.\n")
        input("Press any key to continue...")

    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        print("Rolling back changes...", file=sys.stderr)
        restore_file_contents()
        rollback_renames()
        print("Rollback complete.", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    print("GDExtension Plugin Renaming Tool by @realNikich\n")
    print("Warning: Plugin names are normalized to lowercase for proper Godot convention.")
    print("Warning: Ensure your target test project is closed before running this script.")
    print("\n")

    new_plugin_name = input("Please enter your new plugin name or 'q' to quit: ").strip()

    if new_plugin_name.lower() == 'q':
        print("Quitting...")
        sys.exit(0)

    sanitized = sanitize_and_validate_filename(new_plugin_name)

    if not sanitized:
        print("Error: Invalid plugin name format.", file=sys.stderr)
        sys.exit(1)
    else:
        update_plugin_name(sanitized)
