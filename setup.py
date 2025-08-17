import os
import sys
import subprocess

script_dir = os.path.dirname(os.path.abspath(__file__))

def clear_screen():
    """Clear the terminal screen cross-platform."""
    os.system('cls' if os.name == 'nt' else 'clear')


def read_dont_touch():
    """Read the first and second lines from dont_touch.txt."""
    try:
        with open(os.path.join(script_dir, "dont_touch.txt"), "r") as file:
            lines = file.readlines()
            if len(lines) < 2:
                raise ValueError("dont_touch.txt does not have at least two lines")
            return lines[0].strip(), lines[1].strip()
    except (FileNotFoundError, IOError, ValueError):
        print("Someone deleted or modified the dont_touch.txt file used for storing important data.")
        print("You can't use the functionality anymore. You'll have to edit the appropriate files manually.")
        print("Check the scripts inside the tools folder for guidance.")
        sys.exit(1)


def display_start_screen():
    print("\n=== Godot C++ GDExtension Setup Tool By @realNikich ===\n")
    print("Official GitHub Repository: https://github.com/nikoladevelops/godot-plus-plus")
    print("Find Godot GDExtension Tutorials Here: https://youtube.com/@realNikich\n")
    input("Press any key to continue...")
    clear_screen()


def display_menu():
    """Display the main menu options."""
    clear_screen()

    first_line, second_line = read_dont_touch()
    
    print(f"Current Plugin Name: {first_line}")
    print(f"Current Targeted Godot Version: {second_line}\n")

    print("Choose an option:")
    print("1. Change Godot Target Version")
    print("2. Change Build Profile")
    print("3. Rename Plugin")
    print("4. Compile Plugin Debug Build")
    print("5. Generate Missing XML Documentation Files")
    print("Enter your choice (1-5), or 'q' to quit: ")


def run_tool_script(script_filename):
    """Run a script from the tools folder and handle errors/output."""
    script_path = os.path.join(script_dir, "tools", script_filename)
    result = subprocess.run([sys.executable, script_path])
    
    if result.returncode != 0:
        print(result.stderr or "An error occurred.")
        input("Press Enter to continue...")


def handle_option(choice):
    """Handle the selected menu option."""
    clear_screen()
    script_map = {
        '1': "change_version.py",
        '2': "change_build_profile.py",
        '3': "renaming.py",
        '4': "compile_debug_build.py",
        '5': "generate_xml_docs.py",
    }

    script_name = script_map.get(choice)
    if script_name:
        run_tool_script(script_name)
    else:
        print("Invalid option.")
        input("Press Enter to continue...")


def main():
    """Main loop to display menu and handle user input."""
    display_start_screen()
    valid_choices = {'1', '2', '3', '4', '5'}

    while True:
        display_menu()
        user_input = input().strip().lower()

        if user_input == 'q':
            print("Quitting...")
            sys.exit(0)

        if user_input not in valid_choices:
            print("Invalid choice. Please enter a valid option or 'q' to quit.")
            input("Press Enter to continue...")
            continue

        handle_option(user_input)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nQuitting...")
        sys.exit(0)
