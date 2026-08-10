import sys

from git_helpers import (
    check_godot_cpp_has_updates,
    check_godot_cpp_submodule_initialized,
    initialize_current_submodule_version,
    update_submodule_to_latest_master_commit,
)


def update_godot_cpp_version():
    print("Tool For Updating godot_cpp Version By @realNikich\n")

    print("By updating the submodule version, bug fixes will be applied and new target versions of Godot could become available.\n")

    if not check_godot_cpp_submodule_initialized():
        initialize_current_submodule_version()

    if check_godot_cpp_has_updates():
        while True:
            user_input = (
                input(
                    "There is a new version of godot_cpp available. "
                    "Do you want to update to latest? [y/n/q]: "
                )
                .strip()
                .lower()
            )

            if user_input in ("q", "n"):
                print("Skipping update. Quitting...")
                sys.exit(0)
            elif user_input == "y":
                break
            else:
                print("Invalid choice! Please enter 'y' for Yes, 'n' for No, or 'q' to Quit.\n")

        update_submodule_to_latest_master_commit()

        print("Submodule successfully updated!")
        input("\nPress Enter to continue...")
    else:
        print("You are already running the latest godot_cpp version. No updates available.")
        input("\nPress Enter to continue...")
        sys.exit(0)


if __name__ == "__main__":
    try:
        update_godot_cpp_version()
    except KeyboardInterrupt:
        print("\nOperation cancelled by user.")
        sys.exit(0)
