import os
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path

from config_manager import config
from scons_helpers import clear_screen

GODOT_PATTERN = re.compile(r"Godot|godot", re.IGNORECASE)


def display_info() -> None:
    print("-" * 75)
    print("ABOUT GODOT ENGINE PATH SWITCHER:")
    print("-" * 75)
    print("• Executable Path Usage:")
    print("  Set the local path to your Godot Engine executable.")
    print("  This path is used by helper tools to run editor commands,")
    print("  generate GDExtension XML documentation, and execute tests.")
    print()
    print("• Target Version Matching:")
    print(f"  Current Target Godot Version: {config.getGodotVersion()}")
    print("  Ensure your selected Godot executable matches this version")
    print("  to prevent API mismatch errors.")
    print("-" * 75 + "\n")


def normalize_path(user_input: str) -> Path | None:
    """
    Sanitizes raw path input across Linux, macOS, and Windows.
    Strips quotes, escaped terminal spaces, expands user tildes (~), and resolves symlinks.
    """
    cleaned = user_input.strip().strip('"').strip("'")
    if not cleaned:
        return None

    if platform.system() != "Windows":
        cleaned = cleaned.replace("\\ ", " ")

    try:
        return Path(cleaned).expanduser().resolve()
    except (OSError, ValueError, RuntimeError) as e:
        print(f"Error resolving path '{cleaned}': {e}")
        return None


def handle_macos_app_bundle(path: Path) -> Path:
    """
    Resolves macOS .app bundle folders down to the inner binary executable.
    Handles 'Godot.app', trailing slashes, and direct paths to the MacOS binary.
    """
    if platform.system() == "Darwin" and path.is_dir() and (path.suffix == ".app" or path.name.endswith(".app")):
        macos_dir = path / "Contents" / "MacOS"
        if macos_dir.is_dir():
            for bin_file in macos_dir.iterdir():
                if bin_file.is_file() and GODOT_PATTERN.search(bin_file.name):
                    return bin_file
    return path


def find_godot_in_dir(directory: Path) -> Path | None:
    """Searches a given directory for a binary executable matching 'Godot' or 'godot'."""
    if not directory.is_dir():
        return None

    ext = ".exe" if platform.system() == "Windows" else ""

    try:
        for entry in directory.iterdir():
            if entry.is_file() and GODOT_PATTERN.search(entry.name) and (not ext or entry.name.lower().endswith(ext)):
                return entry
    except PermissionError:
        pass

    return None


def validate_godot_binary(exe_path: Path) -> str | None:
    """Runs `<exe_path> --version` cross-platform to verify binary and extract version string."""
    if not exe_path.is_file():
        return None

    try:
        result = subprocess.run(
            [str(exe_path), "--version"],
            capture_output=True,
            text=True,
            timeout=5,
            check=True,
            shell=platform.system() == "Windows",
        )
        output = result.stdout.strip()
        if output:
            return output
    except (subprocess.SubprocessError, FileNotFoundError, OSError, PermissionError):
        pass
    return None


def sync_and_validate_saved_paths() -> None:
    """
    Pre-flight check: Validates currently saved paths and active path against disk.
    - Updates version string in config if binary was updated/changed.
    - Removes non-existent or invalid paths from savedGodotPaths.
    - Clears godotActivePath if the active binary is missing or invalid.
    """
    # Validate Active Path
    active_path = config.getGodotActivePath()
    if active_path:
        active_obj = Path(active_path)
        if not active_obj.exists():
            config.setGodotActivePath("")
        else:
            active_exe = handle_macos_app_bundle(active_obj)
            if not validate_godot_binary(active_exe):
                config.setGodotActivePath("")

    # Validate Saved Paths Collection
    saved_entries = config.getSavedGodotPaths()
    if not saved_entries:
        return

    for entry in saved_entries:
        path_str = entry.get("path", "")
        if not path_str:
            continue

        path_obj = Path(path_str)
        if not path_obj.exists():
            config.removeSavedGodotPath(path_str)
            continue

        exe_path = handle_macos_app_bundle(path_obj)
        current_ver = validate_godot_binary(exe_path)

        if current_ver and current_ver != entry.get("version"):
            config.addSavedGodotPath(path_str, version=current_ver)
        elif not current_ver:
            config.removeSavedGodotPath(path_str)


def auto_detect_godot_paths() -> list[tuple[Path, str]]:
    """
    Scans system PATH, package managers (Flatpak/Snap), Steam libraries,
    and common user folders (~/Desktop, ~/Downloads, etc.) across OSs.
    """
    found_entries = []
    seen_paths = set()

    def add_if_godot(file_path: Path):
        try:
            resolved = file_path.resolve()
            if resolved.exists() and str(resolved) not in seen_paths:
                exe = handle_macos_app_bundle(resolved)
                ver = validate_godot_binary(exe)
                if ver:
                    seen_paths.add(str(exe))
                    found_entries.append((exe, ver))
        except (PermissionError, OSError):
            pass

    for cmd in ["godot", "godot4", "Godot", "org.godotengine.Godot"]:
        path_str = shutil.which(cmd)
        if path_str:
            add_if_godot(Path(path_str))

    system = platform.system()
    home = Path.home()
    search_dirs = []

    if system == "Windows":
        search_dirs.extend([
            Path("C:/Program Files"),
            Path("C:/Program Files (x86)"),
            Path("C:/Godot"),
            Path(os.path.expandvars("%LOCALAPPDATA%")),
            Path("C:/Program Files (x86)/Steam/steamapps/common"),
            Path("C:/Program Files/Steam/steamapps/common"),
        ])
    elif system == "Darwin":
        search_dirs.extend([
            Path("/Applications"),
            home / "Applications",
            home / "Library/Application Support/Steam/steamapps/common",
        ])
    else:
        search_dirs.extend([
            Path("/usr/bin"),
            Path("/usr/local/bin"),
            home / ".local/bin",
            Path("/opt"),
            Path("/snap/bin"),
            Path("/var/lib/flatpak/exports/bin"),
            home / ".local/share/flatpak/exports/bin",
            home / ".local/share/Steam/steamapps/common",
            home / ".steam/steam/steamapps/common",
        ])

    user_folders = [
        home / "Desktop",
        home / "Downloads",
        home / "bin",
        home / "Godot",
        home / "Tools",
        home / "Development",
    ]
    for uf in user_folders:
        if uf.exists():
            search_dirs.append(uf)

    for search_dir in search_dirs:
        if not search_dir.exists():
            continue

        try:
            if system == "Darwin":
                for app in search_dir.glob("*.app"):
                    if GODOT_PATTERN.search(app.name):
                        add_if_godot(app)

            exe = find_godot_in_dir(search_dir)
            if exe:
                add_if_godot(exe)

            for sub_dir in search_dir.iterdir():
                if sub_dir.is_dir() and not sub_dir.name.startswith("."):
                    if system == "Darwin" and sub_dir.name.endswith(".app") and GODOT_PATTERN.search(sub_dir.name):
                        add_if_godot(sub_dir)
                    else:
                        sub_exe = find_godot_in_dir(sub_dir)
                        if sub_exe:
                            add_if_godot(sub_exe)

        except (PermissionError, OSError):
            continue

    return found_entries


def add_custom_path_prompt() -> None:
    print("\nPaste the file or folder path to your Godot Engine executable:")
    user_input = input("> ").strip()

    if not user_input:
        return

    path = normalize_path(user_input)
    if not path or not path.exists():
        print("\nError: Provided path does not exist on disk!")
        input("Press Enter to continue...")
        return

    resolved_path = handle_macos_app_bundle(path)

    if resolved_path.is_dir():
        exe = find_godot_in_dir(resolved_path)
        if exe:
            resolved_path = exe
        else:
            print("\nError: No Godot executable found inside that directory.")
            input("Press Enter to continue...")
            return

    version_str = validate_godot_binary(resolved_path) or "Unknown Version"
    if version_str == "Unknown Version":
        print(f"\nWarning: '{resolved_path.name}' failed version validation (`--version`).")
        confirm = input("Save this path anyway? (y/n): ").strip().lower()
        if confirm != "y":
            return
    else:
        print(f"\nValidated Godot Version: {version_str}")
        target_version = config.getGodotVersion()
        if target_version not in version_str:
            print(f"Warning: Executable version '{version_str}' does not match target version '{target_version}'.")

    path_str = str(resolved_path)
    config.addSavedGodotPath(path_str, version=version_str)

    if not config.getGodotActivePath():
        config.setGodotActivePath(path_str)
        print(f"\nSuccessfully added and set active Godot path: {path_str}")
    else:
        print(f"\nSuccessfully added path to saved list: {path_str}")

    input("Press Enter to continue...")


def run_auto_detection() -> None:
    print("\nScanning system for Godot engine executables...")
    discovered = auto_detect_godot_paths()

    if not discovered:
        print("No Godot executables automatically discovered.")
    else:
        print(f"\nDiscovered {len(discovered)} Godot executable(s):")
        for exe, ver in discovered:
            path_str = str(exe)
            print(f"  • {path_str} ({ver})")
            config.addSavedGodotPath(path_str, version=ver)

        if not config.getGodotActivePath() and discovered:
            first_path = str(discovered[0][0])
            config.setGodotActivePath(first_path)
            print(f"\nAuto-selected active Godot path: {first_path}")

        print("\nDiscovered paths saved to configuration.")

    input("Press Enter to continue...")


def switch_path_prompt() -> None:
    saved_entries = config.getSavedGodotPaths()
    if not saved_entries:
        print("\nNo saved paths available to switch to.")
        input("Press Enter to continue...")
        return

    print("\nSelect path number to activate:")
    for idx, entry in enumerate(saved_entries, start=1):
        p, v = entry["path"], entry["version"]
        is_active = " (Active)" if p == config.getGodotActivePath() else ""
        print(f"  [{idx}] {p} ({v}){is_active}")

    choice = input("\nEnter number (or 'q' to cancel): ").strip().lower()
    if choice.isdigit() and 1 <= int(choice) <= len(saved_entries):
        selected_path = saved_entries[int(choice) - 1]["path"]
        config.setGodotActivePath(selected_path)
        print(f"\nActive Godot path set to: {selected_path}")
        input("Press Enter to continue...")


def remove_path_prompt() -> None:
    saved_entries = config.getSavedGodotPaths()
    if not saved_entries:
        print("\nNo saved paths to remove.")
        input("Press Enter to continue...")
        return

    print("\nSelect path number to REMOVE from saved collection:")
    for idx, entry in enumerate(saved_entries, start=1):
        is_active = " [CURRENTLY ACTIVE]" if entry["path"] == config.getGodotActivePath() else ""
        print(f"  [{idx}] {entry['path']} ({entry['version']}){is_active}")

    choice = input("\nEnter number to remove (or 'q' to cancel): ").strip().lower()
    if choice.isdigit() and 1 <= int(choice) <= len(saved_entries):
        target_remove = saved_entries[int(choice) - 1]["path"]
        config.removeSavedGodotPath(target_remove)
        print(f"\nRemoved path from collection: {target_remove}")
        input("Press Enter to continue...")


def select_godot_path() -> None:
    while True:
        sync_and_validate_saved_paths()

        clear_screen()
        print("Godot Engine Path Switcher Tool by @realNikich\n")

        display_info()

        active_path = config.getGodotActivePath()
        saved_entries = config.getSavedGodotPaths()

        if not active_path:
            print("Active Godot Path: NONE (No valid Godot executable active)\n")
        else:
            print(f"Active Godot Path: '{active_path}'\n")

        print("Saved Godot Executables:")
        if not saved_entries:
            print("  (No saved paths yet)")
        else:
            for idx, entry in enumerate(saved_entries, start=1):
                p, v = entry["path"], entry["version"]
                is_active = " [ACTIVE]" if p == active_path else ""
                print(f"  [{idx}] {p} ({v}){is_active}")

        print("\nOptions:")
        print("  [a] Auto-detect Godot executables on system")
        print("  [p] Paste / add custom Godot path manually")
        print("  [s] Switch active Godot path")
        print("  [r] Remove a saved path")
        print("  [q] Quit")

        choice = input("\nSelect option or path number: ").strip().lower()

        if choice == "q":
            sys.exit(0)
        elif choice == "a":
            run_auto_detection()
        elif choice == "p":
            add_custom_path_prompt()
        elif choice == "s":
            switch_path_prompt()
        elif choice == "r":
            remove_path_prompt()
        elif choice.isdigit() and 1 <= int(choice) <= len(saved_entries):
            selected_path = saved_entries[int(choice) - 1]["path"]
            config.setGodotActivePath(selected_path)
            print(f"\nActive Godot path set to: {selected_path}")
            input("Press Enter to continue...")


if __name__ == "__main__":
    try:
        select_godot_path()
    except KeyboardInterrupt:
        print("\nOperation cancelled.")
        sys.exit(0)
