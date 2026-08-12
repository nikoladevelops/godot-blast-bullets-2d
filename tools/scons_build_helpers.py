import os
from pathlib import Path

from paths import get_plugin_dir

from tools.config_manager import config
from tools.git_helpers import (
    check_godot_cpp_submodule_initialized,
    initialize_current_submodule_version,
)
from tools.paths import BUILD_PROFILES_DIR


def find_sources(dirs: list[str], exts: list[str]) -> list[str]:
    """Recursively searches the specified directories for source files."""
    sources = []
    for d in dirs:
        for root, _, files in os.walk(d):
            for file in files:
                if any(file.endswith(ext) for ext in exts):
                    sources.append(os.path.join(root, file))
    return sources


def verify_godot_cpp_submodule() -> None:
    """Checks if godot-cpp submodule is ready; if not, triggers initialization via git_helpers."""
    if not check_godot_cpp_submodule_initialized():
        print("godot-cpp submodule is not initialized yet.")
        initialize_current_submodule_version()


def setup_build_environment(env, opts) -> None:
    """Configures build profiles and updates environment options."""
    selected_profile = config.getSelectedBuildProfile()
    if selected_profile != "none":
        env["build_profile"] = str(BUILD_PROFILES_DIR / selected_profile)
        print(f"Selected build profile: {selected_profile}\n")
    else:
        print("No selected build profile\n")

    opts.Update(env)


def get_library_filename(env, libname: str, precision: str) -> str:
    """Computes standard library suffix and filename conventions based on platform/target."""
    arch_suffix = f".{env['arch']}" if env.get('arch') and env['arch'] != 'universal' else ''
    threads_val = str(env.get('threads', 'no')).strip().lower()
    threads_suffix = '.threads' if env['platform'] == 'web' and threads_val in ('yes', 'true') else ''

    suffix = f".{env['platform']}.{env['target']}{arch_suffix}{threads_suffix}.{precision}"
    return f"{env.subst('$SHLIBPREFIX')}{libname}{suffix}{env.subst('$SHLIBSUFFIX')}"


def get_target_install_dir(env) -> Path:
    """Resolves and ensures the active target project's installation directory exists."""
    install_dir = get_plugin_dir() / "bin" / env['platform']
    os.makedirs(str(install_dir), exist_ok=True)
    return install_dir
