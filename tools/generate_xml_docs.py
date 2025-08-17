import os
import sys
import subprocess
from pathlib import Path
import platform
import re
from typing import Optional

# Constants
TOOL_HEADER = "Tool For Generating XML Editor Documentation By @realNikich"
SCRIPT_DIR = Path(__file__).resolve().parent
ROOT_DIR = SCRIPT_DIR.parent
PROJECT_DIR = ROOT_DIR / "test_project"
DONT_TOUCH_FILE = ROOT_DIR / "dont_touch.txt"
GODOT_PATTERN = r"Godot|godot"
VALID_EXECUTABLE_EXTENSIONS = {
    "Windows": ".exe",
    "Linux": "",
    "Darwin": ""  # macOS
}
DOCS_OUTPUT_DIR = ROOT_DIR / "doc_classes"
GODOT_DOCS_URL = "https://docs.godotengine.org/en/stable/tutorials/scripting/gdextension/gdextension_docs_system.html#documentation-styling"

def print_header() -> None:
    """Display the tool's header and instructions."""
    print(f"\n{'=' * 80}")
    print(f"{TOOL_HEADER:^80}")
    print(f"{'=' * 80}")
    print("This tool generates XML documentation for your GDExtension plugin.")

def display_warning() -> None:
    """Display warnings about requirements for generating documentation."""
    print(f"\n{'!' * 80}")
    print("WARNING: The Godot Engine test_project MUST be open in the Godot Editor!")
    print(f"Please open '{PROJECT_DIR / 'project.godot'}' in Godot and keep the editor running.")
    print("Documentation will NOT be generated if the project is not open.")
    print("\nWARNING: Before trying to generate XML documentation, ensure you have a 'bin' folder")
    print("with the necessary binaries. If you have used the renaming feature recently, you must")
    print("recompile the project and try again.")
    print(f"{'!' * 80}")
    input("\nPress any key to continue... ")

def normalize_path(user_input: str) -> Optional[Path]:
    """Convert user input into a resolved Path object, handling quotes and errors."""
    cleaned_input = user_input.strip().strip('"').strip("'")
    try:
        return Path(cleaned_input).expanduser().resolve()
    except Exception as e:
        print(f"Error normalizing path '{cleaned_input}': {e}")
        return None

def handle_macos_app_bundle(path: Path) -> Path:
    """Resolve macOS .app bundles to the internal executable."""
    if platform.system() == "Darwin" and path.suffix == ".app":
        executable_path = path / "Contents" / "MacOS" / "Godot"
        return executable_path if executable_path.is_file() else path
    return path

def find_godot_executable_in_dir(directory: Path) -> Optional[str]:
    """Search for a Godot executable in the given directory."""
    os_name = platform.system()
    ext = VALID_EXECUTABLE_EXTENSIONS.get(os_name, "")
    godot_regex = re.compile(GODOT_PATTERN + re.escape(ext) + r"$", re.IGNORECASE)

    if not directory.is_dir():
        return None

    for entry in directory.iterdir():
        if entry.is_file() and godot_regex.search(entry.name):
            return str(entry)
    return None

def check_system_path() -> Optional[str]:
    """Check if 'godot' is in the system PATH and is a valid executable."""
    print("Checking for Godot executable in system PATH...")
    try:
        result = subprocess.run(
            ["godot", "--version"],
            capture_output=True,
            text=True,
            timeout=5,
            check=True,
            shell=platform.system() == "Windows"
        )
        if "Godot Engine" in result.stdout:
            print("Godot executable found in system PATH.")
            return "godot"
    except (subprocess.CalledProcessError, FileNotFoundError, subprocess.TimeoutExpired):
        print("Godot executable not found in system PATH.")
    return None

def read_cached_path() -> Optional[str]:
    """Read and validate the cached Godot executable path from dont_touch.txt."""
    print(f"Checking for cached path in '{DONT_TOUCH_FILE}'...")
    if not DONT_TOUCH_FILE.exists():
        print("Cache file does not exist.")
        return None

    try:
        with open(DONT_TOUCH_FILE, "r", encoding="utf-8") as f:
            lines = f.readlines()

        if len(lines) < 3:
            print("Cache file is malformed or too short.")
            return None

        cached_path = lines[2].strip()
        path = Path(cached_path)
        resolved_path = handle_macos_app_bundle(path)

        if resolved_path.is_file() and re.search(GODOT_PATTERN, resolved_path.name, re.IGNORECASE):
            print(f"Valid Godot path found in cache: {cached_path}")
            return cached_path
        else:
            print(f"Invalid path in cache: '{cached_path}'. File is missing or not a valid Godot executable.")
            return None
    except Exception as e:
        print(f"Error reading cache file: {e}")
        return None

def prompt_for_path() -> Optional[str]:
    """Prompt user for a Godot executable path, handling files and directories."""
    print("\n")
    print("No valid Godot executable found.")
    print("Type 'q' to quit or enter path manually.\n")
    while True:
        user_input = input(
            "Enter the folder path or full path to your Godot executable "
            "(if a folder, the executable must be directly inside): "
        ).strip()
        if user_input.lower() == "q":
            sys.exit(0)

        path = normalize_path(user_input)
        if not path or not path.exists():
            print("Path does not exist or is invalid. Please try again.\n")
            continue

        resolved_path = handle_macos_app_bundle(path)
        if resolved_path.is_file() and re.search(GODOT_PATTERN, resolved_path.name, re.IGNORECASE):
            return str(resolved_path)
        elif resolved_path.is_dir():
            exe_path = find_godot_executable_in_dir(resolved_path)
            if exe_path:
                return exe_path
            else:
                print(
                    "No Godot executable found in directory (looking for 'Godot' or 'godot' in filename). "
                    "Ensure it’s directly inside or provide the full path.\n"
                )
        else:
            print("Path is neither a file nor a directory. Please try again.\n")

def update_cached_path(new_path: str) -> None:
    """Update the cached Godot executable path in dont_touch.txt."""
    print(f"Caching new Godot executable path to '{DONT_TOUCH_FILE}'...")
    lines = []
    if DONT_TOUCH_FILE.exists():
        try:
            with open(DONT_TOUCH_FILE, "r", encoding="utf-8") as f:
                lines = f.readlines()
        except Exception as e:
            print(f"Warning: Could not read existing cache file. Creating a new one. Error: {e}")

    while len(lines) < 3:
        lines.append("\n")
    lines[2] = f"{new_path}\n"

    try:
        DONT_TOUCH_FILE.parent.mkdir(parents=True, exist_ok=True)
        with open(DONT_TOUCH_FILE, "w", encoding="utf-8") as f:
            f.writelines(lines)
        print("Cache updated successfully.")
    except Exception as e:
        print(f"Warning: Failed to update cache file. Error: {e}")

def validate_project_directory() -> bool:
    """Validate that the project directory contains a valid Godot project."""
    if not PROJECT_DIR.is_dir() or not (PROJECT_DIR / "project.godot").is_file():
        print(f"Error: The project directory at '{PROJECT_DIR}' is not a valid Godot project.")
        return False
    return True

def generate_docs(godot_exec: str) -> bool:
    """Run the Godot executable to generate documentation."""
    if not validate_project_directory():
        return False

    print(f"\nUsing Godot executable: {godot_exec}")
    print(f"Project directory (CWD): {PROJECT_DIR}")
    print(f"Documentation output directory: {ROOT_DIR}")

    command = [
        str(godot_exec),  # Ensure path is string for compatibility
        "--doctool",
        str(ROOT_DIR.resolve()),  # Absolute path for output directory
        "--gdextension-docs"
    ]

    print(f"Running command: {' '.join(command)}")

    try:
        result = subprocess.run(
            command,
            cwd=str(PROJECT_DIR.resolve()),  # Run from project directory
            capture_output=True,
            text=True,
            check=True,
            encoding="utf-8"
        )
        print("\nDocumentation generated successfully.")
        print(f"Godot Output (stdout):\n{result.stdout.strip()}")
        return True
    except subprocess.CalledProcessError as e:
        print("\nERROR: Failed to generate documentation. Godot exited with an error.")
        print(f"Error Details (stderr):\n{e.stderr.strip()}")
        print(f"Output (stdout):\n{e.stdout.strip()}\n")
        return False
    except FileNotFoundError:
        print(f"\nERROR: The executable '{godot_exec}' was not found. Please check the path.")
        return False
    except Exception as e:
        print(f"\nAn unexpected error occurred while running the executable: {e}\n")
        return False

def main() -> None:
    """Main entry point for the script."""
    print_header()
    display_warning()

    godot_executable = check_system_path() or read_cached_path() or prompt_for_path()

    if godot_executable:
        update_cached_path(godot_executable)
        if generate_docs(godot_executable):
            print(f"\nDone! Check the '{DOCS_OUTPUT_DIR}' folder and add custom documentation to the files!")
            print(f"Find out more here: {GODOT_DOCS_URL}")
            print("\nAfter writing your custom documentation inside the files, you need to recompile again!")
    else:
        print("\nExiting. No Godot executable could be found or provided.")

    input("\nPress Enter to exit...")

if __name__ == "__main__":
    main()