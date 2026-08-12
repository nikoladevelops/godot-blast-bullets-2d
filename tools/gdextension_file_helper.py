import filecmp
import re
from pathlib import Path

from paths import get_gdextension_file_path, get_plugin_dir


def sync_gdextension_to_target_project() -> None:
    """
    Copies the master source .gdextension file from the template workspace root
    into the active target Godot project's plugin folder if they differ or if it's missing.
    Also removes any stale Godot import cache (.import) files to force a clean re-import.
    """
    source_path = get_gdextension_file_path()
    if not source_path.exists():
        return

    plugin_dir = get_plugin_dir()
    plugin_dir.mkdir(parents=True, exist_ok=True)

    dest_path = plugin_dir / source_path.name
    import_file_path = Path(f"{dest_path}.import")

    needs_copy = False
    if not dest_path.exists():
        needs_copy = True
    else:
        if not filecmp.cmp(source_path, dest_path, shallow=False):
            needs_copy = True

    if needs_copy:
        plugin_dir.mkdir(parents=True, exist_ok=True)
        dest_path.write_text(source_path.read_text(encoding="utf-8"), encoding="utf-8")
        print(f"Synced .gdextension manifest to target project: {dest_path}")

        if import_file_path.exists():
            try:
                import_file_path.unlink()
                print(f"Removed stale import cache: {import_file_path}")
            except OSError as e:
                print(f"Warning: Could not remove import file: {e}")

def purge_old_project_gdextension(old_name: str) -> None:
    """
    Safely purges the old plugin's .gdextension and .uid files from the target project directory
    to prevent file-locking conflicts or stale metadata when renaming.
    """
    plugin_dir = get_plugin_dir().parent / old_name  # parent of plugin dir is addons/
    if not plugin_dir.exists():
        return

    for ext_file in plugin_dir.glob(f"{old_name}.gdextension*"):
        try:
            ext_file.unlink()
            print(f"Purged stale manifest file: {ext_file.name}")
        except OSError as e:
            print(f"Warning: Could not remove locked file {ext_file.name}: {e}")


def set_editor_target_mode(mode: str, file_path: Path | None = None, sync: bool = True) -> None:
    """
    Updates all `.debug` target entries in [libraries] and [dependencies] inside the
    .gdextension file to point to either 'template_debug' binaries (mode="debug")
    or 'template_release' binaries (mode="release").
    """
    target_path = get_target_gdextension_path(file_path)

    if not target_path.exists():
        raise FileNotFoundError(f"GDExtension file not found at: {target_path}")

    content = target_path.read_text(encoding="utf-8")
    lines = content.splitlines()
    updated_lines = []

    current_section = ""
    in_debug_block = False

    for line in lines:
        stripped = line.strip()

        if stripped.startswith("[") and stripped.endswith("]"):
            current_section = stripped.lower()
            in_debug_block = False
            updated_lines.append(line)
            continue

        if current_section in ["[libraries]", "[dependencies]"]:
            if "=" in line and not stripped.startswith(";"):
                key_part = line.split("=", 1)[0].strip()
                in_debug_block = ".debug" in key_part

            if in_debug_block:
                if mode == "release":
                    line = line.replace("template_debug", "template_release")
                else:
                    line = line.replace("template_release", "template_debug")

            if "}" in line:
                in_debug_block = False

        updated_lines.append(line)

    target_path.write_text("\n".join(updated_lines) + "\n", encoding="utf-8")

    if sync:
        sync_gdextension_to_target_project()


def force_editor_target_to_release(file_path: Path | None = None) -> None:
    """
    Forces the .gdextension manifest to point all editor target bindings to
    'template_release' WITHOUT triggering synchronization. Safe for CI environments.
    """
    set_editor_target_mode("release", file_path=file_path, sync=False)


def set_reloadable(reloadable: bool, file_path: Path | None = None) -> None:
    """
    Updates or inserts the `reloadable` key under [configuration]
    in the .gdextension manifest using strict lowercase booleans (reloadable = true / false).
    """
    target_path = get_target_gdextension_path(file_path)

    if not target_path.exists():
        raise FileNotFoundError(f"GDExtension file not found at: {target_path}")

    content = target_path.read_text(encoding="utf-8")
    value_str = "true" if reloadable else "false"

    pattern = r'(reloadable\s*=\s*)(true|false|True|False)'

    if re.search(pattern, content):
        updated_content = re.sub(pattern, f'\\g<1>{value_str}', content)
    else:
        if "[configuration]" in content:
            updated_content = content.replace(
                "[configuration]\n",
                f'[configuration]\nreloadable = {value_str}\n',
                1,
            )
        else:
            updated_content = f'[configuration]\nreloadable = {value_str}\n\n' + content

    target_path.write_text(updated_content, encoding="utf-8")
    sync_gdextension_to_target_project()


def get_target_gdextension_path(custom_path: Path | None = None) -> Path:
    """Utility to resolve custom_path or default to get_gdextension_file_path()."""
    return custom_path if custom_path is not None else get_gdextension_file_path()


def set_compatibility_minimum(
    min_version: str, file_path: Path | None = None
) -> None:
    """
    Updates or inserts the `compatibility_minimum` key under [configuration]
    in the .gdextension manifest. Defaults to paths.get_gdextension_file_path().
    """
    target_path = get_target_gdextension_path(file_path)

    if not target_path.exists():
        raise FileNotFoundError(f"GDExtension file not found at: {target_path}")

    content = target_path.read_text(encoding="utf-8")
    pattern = r'(compatibility_minimum\s*=\s*")[^"]*(")'

    if re.search(pattern, content):
        updated_content = re.sub(pattern, f'\\g<1>{min_version}\\g<2>', content)
    else:
        if "[configuration]" in content:
            updated_content = content.replace(
                "[configuration]\n",
                f'[configuration]\ncompatibility_minimum = "{min_version}"\n',
                1,
            )
        else:
            updated_content = f'[configuration]\ncompatibility_minimum = "{min_version}"\n\n' + content

    target_path.write_text(updated_content, encoding="utf-8")
    sync_gdextension_to_target_project()


def update_section_in_gdextension(
    section_name: str, key_value_pairs: dict[str, str], file_path: Path | None = None
) -> None:
    """
    Safely replaces or appends a target section (e.g., [icons]) inside a .gdextension file.
    """
    target_path = get_target_gdextension_path(file_path)

    if not target_path.exists():
        raise FileNotFoundError(f"GDExtension file not found at: {target_path}")

    lines = target_path.read_text(encoding="utf-8").splitlines()

    new_lines: list[str] = []
    inside_target_section = False
    target_header = f"[{section_name}]"

    for line in lines:
        stripped = line.strip()
        if stripped == target_header:
            inside_target_section = True
            continue

        if inside_target_section and stripped.startswith("[") and stripped.endswith("]"):
            inside_target_section = False

        if not inside_target_section:
            new_lines.append(line)

    clean_content = "\n".join(new_lines).rstrip()

    section_entries = [
        f'{key} = "{value}"' for key, value in sorted(key_value_pairs.items())
    ]
    new_section_block = f"\n\n{target_header}\n" + "\n".join(section_entries) + "\n"

    final_content = clean_content + new_section_block
    target_path.write_text(final_content, encoding="utf-8")
    sync_gdextension_to_target_project()


def update_icons_in_gdextension(
    icon_mappings: dict[str, str], file_path: Path | None = None
) -> None:
    """
    Helper shortcut specifically for updating the [icons] section in a .gdextension file.
    """
    update_section_in_gdextension("icons", icon_mappings, file_path=file_path)
