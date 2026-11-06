"""Copies the built addon (addons/<plugin>/: libraries, .gdextension, icons, docs)
from the main Godot project into every extra project folder of config.json
(extraProjectFolders), so a second project (a game, a demo) always runs the
latest build. A folder that is missing or is not a Godot project is skipped with
one line. Runs after every successful build (scons_helpers.run_scons_build);
run this file to sync by hand.

    python3 tools/sync_project_folders.py
"""
import os
import shutil
import sys
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from config_manager import config  # noqa: E402
from paths import PROJECT_ROOT, get_plugin_dir  # noqa: E402


def resolve(folder: str) -> Path:
    p = Path(folder).expanduser()
    return p if p.is_absolute() else (PROJECT_ROOT / p).resolve()


def sync_extra_project_folders(quiet: bool = False) -> list:
    """Returns [(folder, status)]: 'synced', 'missing', 'not a Godot project' or 'same as the main project'."""
    source = get_plugin_dir()
    results = []
    for folder in config.getExtraProjectFolders():
        target_project = resolve(folder)
        if not target_project.is_dir():
            status = "missing"
        elif not (target_project / "project.godot").is_file():
            status = "not a Godot project"
        elif target_project.resolve() == source.parent.parent.resolve():
            status = "same as the main project"
        elif not source.is_dir():
            status = "nothing built yet"
        else:
            shutil.copytree(source, target_project / "addons" / source.name, dirs_exist_ok=True)
            status = "synced"
        results.append((str(target_project), status))
        if not quiet:
            print(f"Extra project {target_project}: {status}")
    return results


if __name__ == "__main__":
    if not config.getExtraProjectFolders():
        print("No extra project folders set (setup.py > Godot Engine & Version Settings > Manage Extra Project Folders).")
    sync_extra_project_folders()
