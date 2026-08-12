import subprocess
import sys

from paths import PROJECT_ROOT, SUBMODULE_PATH


def run_git_command(args, cwd=None):
    """Run a Git command and return (success, output)."""
    # Convert Path objects to string if passed to cwd
    if cwd:
        cwd = str(cwd)

    result = subprocess.run(
        ["git"] + args,
        cwd=cwd,
        text=True,
        capture_output=True,
        check=False
    )
    return result.returncode == 0, result.stdout.strip() or result.stderr.strip()


def check_godot_cpp_submodule_initialized() -> bool:
    """Check whether the godot_cpp submodule is initialized."""
    # In git submodules, .git can be a directory OR a file containing the gitdir pointer
    git_path = SUBMODULE_PATH / ".git"
    return git_path.exists()


def initialize_current_submodule_version() -> None:
    """Attempts to initialize the godot_cpp submodule."""
    print("Attempting to initialize godot-cpp submodule...\n")
    success, output = run_git_command(["submodule", "update", "--init", "--recursive"], cwd=PROJECT_ROOT)

    if not success:
        print("Failed to initialize godot-cpp submodule:")
        print(output)
        sys.exit(1)

    print("Successfully initialized godot-cpp submodule.\n")


def check_godot_cpp_has_updates() -> bool:
    """
    Fetches remote changes, displays local vs. remote commit hashes for master,
    and returns True if updates are available.
    """
    if not check_godot_cpp_submodule_initialized():
        print("Submodule is not initialized yet.")
        return False

    print("Fetching latest updates for godot-cpp...")

    # Fetch latest changes inside the submodule directory
    success, output = run_git_command(["fetch", "origin"], cwd=SUBMODULE_PATH)
    if not success:
        print(f"Failed to fetch updates for submodule: {output}")
        return False

    # Get local commit hash (HEAD)
    _, local_commit = run_git_command(["rev-parse", "--short", "HEAD"], cwd=SUBMODULE_PATH)

    # Get remote commit hash (origin/master)
    success, remote_commit = run_git_command(["rev-parse", "--short", "origin/master"], cwd=SUBMODULE_PATH)
    if not success:
        print(f"Could not reach remote 'origin/master': {remote_commit}")
        return False

    # Display status
    print(f"Current local commit : {local_commit}")
    print(f"Latest remote commit: {remote_commit}\n")

    # If the commit hashes differ, an update is available
    return local_commit != remote_commit


def update_submodule_to_latest_master_commit() -> None:
    """Attempts to update the godot_cpp submodule to the latest remote master branch commit."""
    print("Attempting to update godot-cpp submodule to latest master commit...\n")

    # Checkout the master branch inside the submodule
    success, output = run_git_command(["checkout", "master"], cwd=SUBMODULE_PATH)
    if not success:
        print(f"Failed to checkout master branch: {output}")
        sys.exit(1)

    # Pull the latest commits from origin master
    success, output = run_git_command(["pull", "origin", "master"], cwd=SUBMODULE_PATH)
    if not success:
        print(f"Failed to pull latest master commits: {output}")
        sys.exit(1)

    # Update nested submodules if godot-cpp has any
    run_git_command(["submodule", "update", "--init", "--recursive"], cwd=SUBMODULE_PATH)

    print("Successfully updated godot-cpp submodule to latest master!\n")
