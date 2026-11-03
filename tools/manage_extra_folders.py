"""Lists, adds and removes the extra Godot project folders that receive the
addon after every successful build (see sync_project_folders.py)."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from config_manager import config  # noqa: E402
from scons_helpers import clear_screen, pause  # noqa: E402
from sync_project_folders import resolve, sync_extra_project_folders  # noqa: E402


def main() -> None:
    while True:
        clear_screen()
        folders = config.getExtraProjectFolders()
        print("Extra project folders (each gets addons/ copied after every successful build):")
        for i, f in enumerate(folders, start=1):
            p = resolve(f)
            state = "ok" if (p / "project.godot").is_file() else ("missing" if not p.exists() else "not a Godot project")
            print(f"  {i}. {f}  [{state}]")
        if not folders:
            print("  (none)")
        print("\n  a. Add a folder   r. Remove a folder   s. Sync now   q. Back")
        choice = input("Select an option: ").strip().lower()
        if choice == "q":
            return
        if choice == "a":
            path = input("Path of the Godot project folder (absolute, or relative to the repository): ").strip()
            if path and path not in folders:
                config.setExtraProjectFolders(folders + [path])
        elif choice == "r" and folders:
            n = input("Number to remove: ").strip()
            if n.isdigit() and 1 <= int(n) <= len(folders):
                config.setExtraProjectFolders([f for i, f in enumerate(folders, start=1) if i != int(n)])
        elif choice == "s":
            sync_extra_project_folders()
            pause()


if __name__ == "__main__":
    main()
