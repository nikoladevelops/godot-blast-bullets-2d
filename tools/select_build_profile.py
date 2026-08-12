import sys

from config_manager import config
from paths import BUILD_PROFILES_DIR
from scons_helpers import clear_screen, run_scons_clean


def select_build_profile():
    clear_screen()
    print("Select Build Profile Tool by @realNikich\n")

    print("-" * 75)
    print("ABOUT BUILD PROFILES & BEST PRACTICES:")
    print("-" * 75)
    print("• compilation Speed:")
    print("  Build profiles dramatically speed up compile times by stripping out unused Godot")
    print("  classes from the binding headers. If a profile is inaccurate, you can edit it manually.")
    print()
    print("• Recommended Workflow:")
    print("  1. Early Development: Stick to standard '2D Profile' or '3D Profile'.")
    print("  2. Project Completion: Switch to a 'Custom Profile' (auto-detection). This generates")
    print("     the leanest possible binary and saves huge CI/CD build time in GitHub Actions.")
    print()
    print("• IntelliSense & Auto-Completion Warning:")
    print("  Do NOT use auto-detected Custom Profiles when starting a fresh project with no code.")
    print("  Because unused classes are stripped out, your IDE's IntelliSense will not see them,")
    print("  preventing you from auto-completing classes you haven't written yet.")
    print()
    print("• Target Version Warning:")
    print("  Whenever you switch your Godot target version, ALWAYS update/regenerate your build")
    print("  profiles! Godot frequently adds new classes or removes deprecated ones between releases.")
    print("-" * 75 + "\n")

    if not BUILD_PROFILES_DIR.exists():
        BUILD_PROFILES_DIR.mkdir(parents=True, exist_ok=True)

    current_profile = config.getSelectedBuildProfile()
    print("Please Select The New Build Profile")
    print(f"Currently Active Build Profile: {current_profile}\n")

    profiles = sorted(BUILD_PROFILES_DIR.glob("*.json"))

    print("Which Build Profile Do You Want To Use?")
    print("  [0] None (Include all Godot classes - no profile)")

    for idx, prof in enumerate(profiles, start=1):
        is_current = " (Active)" if prof.name == current_profile else ""
        print(f"  [{idx}] {prof.name}{is_current}")

    print("  [q] Quit without changing\n")

    while True:
        user_input = input("Select a build profile or 'q': ").strip().lower()

        if user_input == "q":
            print("Operation cancelled.")
            sys.exit(0)

        if user_input == "0":
            config.setSelectedBuildProfile("none")
            print("\nBuild Profile set to: None (all classes enabled).")
            run_scons_clean()
            input("\nPress Enter to continue...")
            return

        if user_input.isdigit():
            choice_num = int(user_input)
            if 1 <= choice_num <= len(profiles):
                selected_file = profiles[choice_num - 1]
                config.setSelectedBuildProfile(selected_file.name)

                print(f"\nSuccessfully selected Build Profile: {selected_file.name}")
                run_scons_clean()
                print("Please recompile your project for the profile changes to take effect.")
                input("\nPress Enter to continue...")
                return

        print(f"Invalid selection! Enter a number between 0 and {len(profiles)}, or 'q'.")


if __name__ == "__main__":
    try:
        select_build_profile()
    except KeyboardInterrupt:
        print("\nOperation cancelled.")
        sys.exit(0)
