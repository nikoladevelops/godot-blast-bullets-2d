import os

from tools.apple_helpers import build_apple_framework
from tools.config_manager import config
from tools.gdextension_file_helper import sync_gdextension_to_target_project
from tools.paths import DOCS_SOURCE_DIR
from tools.scons_build_helpers import (
    find_sources,
    get_library_filename,
    get_target_install_dir,
    setup_build_environment,
    verify_godot_cpp_submodule,
)

# Synchronize the master .gdextension manifest to active project target (skipped on CI runners)
if not os.environ.get("CI") and not os.environ.get("SKIP_SYNC"):
    sync_gdextension_to_target_project()
else:
    print("Skipping target project synchronization (CI/Runner environment detected).")

# Base Configuration Setup
libname = config.getPluginName()
godot_version = config.getGodotVersion()

# Automatically inject config.json Godot Target version into SCons arguments so godot-cpp picks it up natively
# (for CI runners that call "scons" without providing the actual target Godot API version)
if "api_version" not in ARGUMENTS and godot_version:
    print(f"Selected Godot API version: {godot_version}")
    ARGUMENTS["api_version"] = godot_version

env = Environment(tools=["default"], PLATFORM="")

customs = [os.path.abspath("custom.py")]
opts = Variables(customs, ARGUMENTS)

# Register GDExtension Build Variables
opts.Add('source_dirs', 'List of source directories', 'src')
opts.Add('source_exts', 'List of source extensions', '.cpp,.c,.cc,.cxx')
opts.Add('include_dirs', 'List of include directories', 'src')
opts.Add('doc_output_dir', 'Directory for documentation output', 'gen')
opts.Add('precision', 'Floating precision (single/double)', 'single')
opts.Add('bundle_id_prefix', 'Bundle identifier prefix', 'com.gdextension')
opts.Add(EnumVariable('threads', 'Enable threads for web', 'no', allowed_values=('yes', 'no', 'true', 'false')))

setup_build_environment(env, opts)
Help(opts.GenerateHelpText(env))

# Verify and Auto-Initialize godot-cpp Submodule
verify_godot_cpp_submodule()

# Include godot-cpp SConstruct script
env = SConscript("godot-cpp/SConstruct", {"env": env, "customs": customs})

# Extract Environment Options & Source Files
source_dirs = env['source_dirs'].split(',')
source_exts = env['source_exts'].split(',')
include_dirs = env['include_dirs'].split(',')
doc_output_dir = env['doc_output_dir']
precision = env.get('precision', 'single')
bundle_id_prefix = env.get('bundle_id_prefix', 'com.gdextension')

env.Append(CPPPATH=include_dirs)
sources = find_sources(source_dirs, source_exts)

# Bind Documentation Generation
is_debug_target = env.get("target") == "template_debug"
editor_target_mode = config.getEditorTargetMode()

if is_debug_target or (env.get("target") == "template_release" and editor_target_mode == "release"):
    try:
        doc_output_file = os.path.join(doc_output_dir, 'doc_data.gen.cpp')
        doc_data = env.GodotCPPDocData(doc_output_file, source=Glob(str(DOCS_SOURCE_DIR / "*.xml")))
        sources.append(doc_data)
    except AttributeError:
        print("Skipping class reference (pre-4.3 baseline target).")

lib_filename = get_library_filename(env, libname, precision)
build_target = env.get('target')

if not build_target:
    raise ValueError("SCons environment is missing the required 'target' variable.")

# Library Build Targets (Delegates Apple Frameworks to helper or builds generic shared library)
if env['platform'] in ['macos', 'ios']:
    library, install_source = build_apple_framework(env, libname, lib_filename, precision, bundle_id_prefix, sources, build_target)
else:
    library = env.SharedLibrary(f"bin/{env['platform']}/{lib_filename}", source=sources)
    install_source = library

# Final Target Installation (Skipped on CI runners since we package from root bin/)
if not os.environ.get("CI") and not os.environ.get("SKIP_SYNC"):
    install_dir = get_target_install_dir(env)
    copy = env.Install(str(install_dir), source=install_source)
    Default([library, copy])
else:
    print("Skipping target project installation (CI/Runner environment detected).")
    Default([library])
