import subprocess
import sys
from pathlib import Path

from config_manager import config
from paths import DOCS_SOURCE_DIR, PROJECT_ROOT, get_godot_project_dir
from scons_helpers import clear_screen

TOOL_HEADER = "Tool For Generating XML Editor Documentation By @realNikich"
GODOT_DOCS_URL = "https://docs.godotengine.org/en/stable/tutorials/scripting/cpp/gdextension_docs_system.html"


def print_header() -> None:
    """Display the tool's header and instructions."""
    print(f"\n{'=' * 80}")
    print(f"{TOOL_HEADER:^80}")
    print(f"{'=' * 80}")
    print("This tool generates XML documentation for your GDExtension plugin.")


def display_warning() -> None:
    """Display warnings about requirements for generating documentation and handle prompt cancellation."""
    project_dir = get_godot_project_dir()
    print(f"\n{'!' * 80}")
    print("IMPORTANT WORKFLOW INSTRUCTIONS:")
    print("1. Ensure the Godot Editor is CLOSED before compiling and generating docs.")
    print("   (Having the editor open locks the GDExtension binary and prevents SCons from")
    print("   updating it, which causes doctool to miss your custom classes).")
    print("2. Ensure you have recently compiled your plugin (debug/release build) so that")
    print("   the test project's 'bin' folder contains the latest compiled binary.")
    print(f"\nTarget Godot Project Directory: {project_dir}")
    print(f"{'!' * 80}")

    choice = input("\nPress Enter to continue, or type 'q' to cancel: ").strip().lower()
    if choice == "q":
        print("\nOperation cancelled by user.")
        sys.exit(0)


def print_documentation_guide() -> None:
    """Print formatting guidelines and BBCode attribute syntax directly to the user."""
    print(f"\n{'=' * 80}")
    print(f"{'GODOT XML DOCUMENTATION QUICK GUIDE':^80}")
    print(f"{'=' * 80}")
    print(f"Once generated, your XML files are located in: '{DOCS_SOURCE_DIR}'")
    print("You can style descriptions using BBCode-style tags inside the text fields:\n")

    print("1. CROSS-REFERENCING CLASSES & MEMBERS:")
    print("    • [Node2D]                    -> Links to another class reference.")
    print("    • [method Node.add_child]    -> Links to a specific method.")
    print("    • [member Node2D.position] -> Links to a class property.")
    print("    • [param spawn_pos]        -> Highlights a method parameter name.")
    print("    • [constant OK]            -> Links to an enum or constant value.\n")

    print("2. FORMATTING & CODE EXAMPLES:")
    print("    • [code]true[/code]         -> Inline monospaced code styling.")
    print("    • [codeblock]")
    print("      var factory = BulletFactory2D.new()")
    print("      factory.spawn()")
    print("      [/codeblock]              -> Multi-line code block display.")
    print("    • [b]bold text[/b]         -> Bold emphasis formatting.")
    print("    • [i]italic text[/i]       -> Italic emphasis formatting.")
    print(f"{'=' * 80}\n")


def validate_project_directory(project_dir: Path) -> bool:
    """Validate that the project directory contains a valid Godot project."""
    if not project_dir.is_dir() or not (project_dir / "project.godot").is_file():
        print(f"Error: The project directory at '{project_dir}' is not a valid Godot project.")
        print("Please check your configuration or run 'Select Godot Project Folder'.")
        return False
    return True


def generate_docs(godot_exec: str, project_dir: Path) -> bool:
    """Run the Godot executable to generate documentation via --doctool."""
    if not validate_project_directory(project_dir):
        return False

    print(f"\nUsing Godot executable: {godot_exec}")
    print(f"Project directory (CWD): {project_dir}")
    print(f"Documentation output directory: {PROJECT_ROOT}")

    command = [
        str(godot_exec),
        "--doctool",
        str(PROJECT_ROOT.resolve()),
        "--gdextension-docs",
    ]

    print(f"Running command: {' '.join(command)}")

    try:
        result = subprocess.run(
            command,
            cwd=str(project_dir.resolve()),
            capture_output=True,
            text=True,
            check=True,
            encoding="utf-8",
        )
        print("\nDocumentation generated successfully.")
        if result.stdout.strip():
            print(f"Godot Output (stdout):\n{result.stdout.strip()}")
        return True
    except subprocess.CalledProcessError as e:
        print("\nERROR: Failed to generate documentation. Godot exited with an error.")
        print(f"Error Details (stderr):\n{e.stderr.strip()}")
        if e.stdout.strip():
            print(f"Output (stdout):\n{e.stdout.strip()}\n")
        return False
    except FileNotFoundError:
        print(f"\nERROR: The executable '{godot_exec}' was not found. Please check your active path.")
        return False
    except Exception as e:
        print(f"\nAn unexpected error occurred while running the executable: {e}\n")
        return False


def main() -> None:
    """Main entry point for the script."""
    clear_screen()
    print_header()

    godot_executable = config.getGodotActivePath()
    project_dir = get_godot_project_dir()

    if not godot_executable:
        print("\n[!] ERROR: No active Godot Engine executable path found in configuration.")
        print("Please run option ('Select Godot Engine Executable Path') from the main setup menu first")
        print("to configure and select a valid Godot executable before generating documentation.")
        input("\nPress Enter to exit...")
        return

    display_warning()

    if generate_docs(godot_executable, project_dir):
        print(f"\nDone! Check the '{DOCS_SOURCE_DIR}' folder and customize your XML files.")
        print_documentation_guide()
        print(f"Official Documentation Reference: {GODOT_DOCS_URL}")
        print("\nAfter writing your custom documentation inside the files, remember to recompile your project!")
    else:
        print("\nDocumentation generation process failed.")

    input("\nPress Enter to exit...")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nOperation cancelled.")
        sys.exit(0)
