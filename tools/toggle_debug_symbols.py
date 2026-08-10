import sys

from config_manager import config
from scons_helpers import clear_screen

if __name__ == "__main__":
    clear_screen()
    current_status = config.getDebugSymbols()

    print("GDExtension Debug Symbols Configuration Tool\n")
    print(f"Current Local Config Status (debugSymbols): {current_status.upper()}")
    print("-" * 65 + "\n")

    print("CRITICAL WARNINGS & BEST PRACTICES:")
    print("-----------------------------------------------------------------")
    print("1. PERFORMANCE & FILE SIZE IMPACT:")
    print("   Enabling debug symbols embeds massive DWARF/PDB metadata blocks")
    print("   into your binaries. While compiler optimizations (like LTO)")
    print("   do the heavy lifting for speed, bloated symbol tables increase")
    print("   binary file sizes drastically and add startup memory overhead.")
    print("\n2. PRODUCTION & RELEASE BUILDS:")
    print("   DO NOT ship release builds with debug symbols enabled unless you")
    print("   specifically intend to analyze crash dumps from the wild. Doing")
    print("   so exposes your proprietary C++ source layout, function names,")
    print("   and file paths, making reverse-engineering effortless for anyone.")
    print("\n3. GITHUB ACTIONS WORKFLOW LIMITATION:")
    print("   THIS OPTION IS LOCAL-ONLY! The GitHub Actions workflow script")
    print("   (build-plugin.yml) does NOT read this configuration variable.")
    print("   If you are building releases via CI/CD workflows and explicitly")
    print("   require debug symbols on cloud runners, you must manually edit")
    print("   the workflow file and append `debug_symbols=\"yes\"` to SCons.")
    print("\n4. DEBUGGER ATTACHMENT:")
    print("   Setting this to 'yes' allows native debuggers (like VS Code,")
    print("   GDB, LLDB, or Zed) to map machine code back to your C++ source")
    print("   files, allowing breakpoints and stack trace inspections.")
    print("-----------------------------------------------------------------\n")

    choice = input("Do you want to enable debug symbols? (y/n) or type 'q' to quit: ").strip().lower()

    if choice == 'q':
        print("Quitting...")
        sys.exit(0)
    elif choice in ['y', 'yes']:
        config.setDebugSymbols("yes")
        print("\n[SUCCESS] Debug symbols ENABLED in config.json.")
        print("Reminder: Turn this off before compiling your final release build to avoid bloated binaries.")
    elif choice in ['n', 'no']:
        config.setDebugSymbols("no")
        print("\n[SUCCESS] Debug symbols DISABLED in config.json.")
    else:
        print("\nInvalid choice. Operation cancelled.")
        input("\nPress Enter to continue...")
        sys.exit(0)

    print("\n" + "=" * 65)
    print("You MUST recompile your plugin for these changes to take effect.")
    print("=" * 65)
    input("\nPress Enter to exit...")
