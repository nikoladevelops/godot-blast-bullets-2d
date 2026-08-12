import sys

from config_manager import config
from scons_helpers import clear_screen


def change_lto_setting():
    clear_screen()
    print("Configure Link Time Optimization (LTO) Tool by @realNikich")

    print("Warning - This affects only the local release build, NOT the github actions workflow (for that check inside .github/workflows)")
    print("Warning - Enabling LTO optimization might cause issues on different platforms. If compilation fails all of a sudden disable it.")

    current_lto = config.getLtoMode()
    print(f"Current LTO Setting: {current_lto}\n")

    options = [
        ("none", "Disabled (Default - Recommended for compatibility & fast compilation)"),
        ("auto", "Auto (Enables best supported LTO method for target toolchain)"),
        ("thin", "Thin LTO (Clang/GCC thin Link Time Optimization)"),
        ("full", "Full LTO (Slower build/link time, max optimizations)"),
    ]

    print("Available options:")
    for idx, (mode, desc) in enumerate(options, start=1):
        marker = " (Current)" if mode == current_lto else ""
        print(f"  [{idx}] {mode.upper()} - {desc}{marker}")

    print("  [q] Quit without changing")

    while True:
        user_input = input("\nSelect an option (1-4) or 'q' to quit: ").strip().lower()

        if user_input == "q":
            print("Operation cancelled. Exiting...")
            sys.exit(0)

        if user_input.isdigit():
            choice_num = int(user_input)
            if 1 <= choice_num <= len(options):
                selected_mode = options[choice_num - 1][0]
                config.setLtoMode(selected_mode)

                print(f"\nSuccessfully set LTO Mode to '{selected_mode}'!")
                input("\nPress Enter to continue...")
                return

        print(f"Invalid selection! Please enter a number between 1 and {len(options)}, or 'q'.")


if __name__ == "__main__":
    try:
        change_lto_setting()
    except KeyboardInterrupt:
        print("\nOperation cancelled by user.")
        sys.exit(0)
