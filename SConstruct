#!/usr/bin/env python
import os
import sys

from methods import print_error

# Function to recursively find .cpp files in the given directories
def find_sources(dirs, exts):
    """
    Recursively searches the specified directories for .cpp files.
    
    Args:
        dirs (list): List of directory paths to search.
        exts (list): List of file extensions that are acceptable and should contain C++ code.
    Returns:
        list: List of full paths to .cpp files found.
    """
    sources = []
    for dir in dirs:
        for root, _, files in os.walk(dir):
            for file in files:
                if any(file.endswith(ext) for ext in exts):
                    sources.append(os.path.join(root, file))
    return sources

# Configuration
libname = "blastbullets2d"
projectdir = "test_project"

# Set up the environment
env = Environment(tools=["default"], PLATFORM="")

# Custom configuration file
customs = ["custom.py"]
customs = [os.path.abspath(path) for path in customs]

# Define GDExtension-specific options
opts = Variables(customs, ARGUMENTS)
opts.Add('source_dirs', 'List of source directories (comma-separated)', 'src') # Directory for source files
opts.Add('source_exts', 'List of source file extensions (comma-separated)', '.cpp,.c,.cc,.cxx') 
opts.Add('include_dirs', 'List of include directories (comma-separated)', 'src') # Directory for headers - some might want to create a separate include directory
opts.Add('doc_output_dir', 'Directory for documentation output', 'gen')
opts.Add('precision', 'Floating-point precision (single or double)', 'single')  # Default to single
opts.Add('bundle_id_prefix', 'Bundle identifier prefix (reverse-DNS format)', 'com.gdextension')  # Default prefix
opts.Add(EnumVariable(
    'threads',
    'Enable threads for web builds',
    'no',  # default
    allowed_values=('yes', 'no', 'true', 'false')
))

# Build profiles can be used to decrease compile times.
# You can either specify "disabled_classes", OR
# explicitly specify "enabled_classes" which disables all other classes.

is_2d_profile_used = "false"
is_3d_profile_used = "false"
is_custom_profile_used = "true"
if is_2d_profile_used:
    env["build_profile"] = "2d_build_profile.json"
elif is_3d_profile_used:
    env["build_profile"] = "3d_build_profile.json"
elif is_custom_profile_used:
    env["build_profile"] = "build_profile.json"

# Update the environment with the options
opts.Update(env)

# Generate help text for the options
Help(opts.GenerateHelpText(env))

# Check for godot-cpp submodule
if not (os.path.isdir("godot-cpp") and os.listdir("godot-cpp")):
    print_error("""godot-cpp is not available within this folder, as Git submodules haven't been initialized.
Run the following command to download godot-cpp:

    git submodule update --init --recursive""")
    sys.exit(1)

# Include godot-cpp SConstruct, passing all command-line arguments
env = SConscript("godot-cpp/SConstruct", {"env": env, "customs": customs})

# Process GDExtension-specific options
source_dirs = env['source_dirs'].split(',')   # Convert comma-separated string to list
source_exts = env['source_exts'].split(',')   # Convert comma-separated string to list
include_dirs = env['include_dirs'].split(',') # Convert comma-separated string to list
doc_output_dir = env['doc_output_dir']        # Directory for documentation output
precision = env.get('precision', 'single')     # Ensure precision defaults to single
bundle_id_prefix = env.get('bundle_id_prefix', 'com.gdextension')  # Ensure prefix defaults to com.gdextension

# Append include directories to CPPPATH
env.Append(CPPPATH=include_dirs)

# Find all .cpp files recursively in the specified source directories
sources = find_sources(source_dirs, source_exts)

# Handle documentation generation if applicable
if env.get("target") in ["editor", "template_debug"]:
    try:
        doc_output_file = os.path.join(doc_output_dir, 'doc_data.gen.cpp')
        doc_data = env.GodotCPPDocData(doc_output_file, source=Glob("doc_classes/*.xml"))
        sources.append(doc_data)
    except AttributeError:
        print("Not including class reference as we're targeting a pre-4.3 baseline.")

# Determine suffixes based on env (align with godot-cpp conventions)
arch_suffix = f".{env['arch']}" if env['arch'] and env['arch'] != 'universal' else ''
# Normalize 'threads' to a lowercase string, defaulting to 'no'
threads_val = str(env.get('threads', 'no')).strip().lower()

# Determine if '.threads' suffix should be added
threads_suffix = '.threads' if env['platform'] == 'web' and threads_val in ('yes', 'true') else ''

suffix = f".{env['platform']}.{env['target']}{arch_suffix}{threads_suffix}.{precision}"
lib_filename = f"{env.subst('$SHLIBPREFIX')}{libname}{suffix}{env.subst('$SHLIBSUFFIX')}"

# Generate Info.plist content for macOS and iOS
def generate_info_plist(platform, target, precision):
    framework_name = f"lib{libname}.{platform}.{target}.{precision}"
    bundle_id = f"{bundle_id_prefix}.{libname}"  # Use configurable prefix
    if platform == 'macos':
        return f"""<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>{framework_name}</string>
    <key>CFBundleIdentifier</key>
    <string>{bundle_id}</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>{framework_name}</string>
    <key>CFBundlePackageType</key>
    <string>FMWK</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0.0</string>
    <key>CFBundleSupportedPlatforms</key>
    <array>
        <string>MacOSX</string>
    </array>
    <key>CFBundleVersion</key>
    <string>1.0.0</string>
    <key>LSMinimumSystemVersion</key>
    <string>10.12</string>
</dict>
</plist>"""
    else:  # ios
        return f"""<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>{framework_name}</string>
    <key>CFBundleIdentifier</key>
    <string>{bundle_id}</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>{framework_name}</string>
    <key>CFBundlePackageType</key>
    <string>FMWK</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0.0</string>
    <key>CFBundleSupportedPlatforms</key>
    <array>
        <string>iPhoneOS</string>
    </array>
    <key>CFBundleVersion</key>
    <string>1.0.0</string>
    <key>LSMinimumSystemVersion</key>
    <string>12.0</string>
</dict>
</plist>"""

# Function to write Info.plist content to a file
def write_info_plist(target, source, env, plist_content):
    with open(target[0].abspath, 'w') as f:
        f.write(plist_content)

# Build the shared library and create frameworks
library = None
install_source = None
if env['platform'] in ['macos', 'ios']:
    # Build the shared library first in bin/{platform}
    temp_lib = env.SharedLibrary(
        f"bin/{env['platform']}/{lib_filename}",
        source=sources
    )
    if env['platform'] == 'macos':
        # Ensure universal if specified
        if env.get('arch') != 'universal':
            env['arch'] = 'universal'  # Fallback to universal for macOS
        framework_name = f"lib{libname}.macos.{env['target']}.{precision}.framework"
        framework_binary = f"lib{libname}.macos.{env['target']}.{precision}"
        framework_dir = f"bin/{env['platform']}/{framework_name}"
        # Create Info.plist file
        plist_file = f"{framework_dir}/Info.plist"
        env.Command(
            plist_file,
            [],
            lambda target, source, env: write_info_plist(target, source, env, generate_info_plist('macos', env['target'], precision))
        )
        # Create the .framework structure in bin/macos
        library = env.Command(
            f"{framework_dir}/{framework_binary}",
            temp_lib,
            [
                f"mkdir -p {framework_dir}",
                f"cp $SOURCE $TARGET",
                f"rm -f bin/{env['platform']}/{lib_filename}"  # Clean up temporary .dylib
            ]
        )
        env.Depends(library, plist_file)  # Ensure Info.plist is created before the framework binary
        install_source = framework_dir
    else:  # iOS
        # Single arm64 build
        if not env.get('arch'):
            env['arch'] = 'arm64'
        temp_framework_name = f"lib{libname}.ios.{env['target']}.{precision}.framework"
        framework_binary = f"lib{libname}.ios.{env['target']}.{precision}"
        framework_name = f"lib{libname}.ios.{env['target']}.{precision}.xcframework"
        temp_framework_dir = f"bin/{env['platform']}/{temp_framework_name}"
        # Create Info.plist file
        plist_file = f"{temp_framework_dir}/Info.plist"
        env.Command(
            plist_file,
            [],
            lambda target, source, env: write_info_plist(target, source, env, generate_info_plist('ios', env['target'], precision))
        )
        # Create temporary .framework in bin/ios
        temp_framework = env.Command(
            f"{temp_framework_dir}/{framework_binary}",
            temp_lib,
            [
                f"mkdir -p {temp_framework_dir}",
                f"cp $SOURCE $TARGET"
            ]
        )
        env.Depends(temp_framework, plist_file)  # Ensure Info.plist is created before the framework binary
        # Create .xcframework in bin/ios
        library = env.Command(
            f"bin/{env['platform']}/{framework_name}",
            temp_framework,
            [
                f"xcodebuild -create-xcframework -framework {temp_framework_dir} -output $TARGET",
                f"rm -rf {temp_framework_dir}",  # Clean up temporary .framework
                f"rm -f bin/{env['platform']}/{lib_filename}"  # Clean up temporary .dylib
            ]
        )
        install_source = f"bin/{env['platform']}/{framework_name}"
else:
    # For other platforms, build a single shared library
    library = env.SharedLibrary(
        f"bin/{env['platform']}/{lib_filename}",
        source=sources
    )
    install_source = library

# Install the library to test_project
install_dir = f"{projectdir}/{libname}/bin/{env['platform']}/"
copy = env.Install(install_dir, source=install_source)

# Set default targets
default_args = [library, copy]
Default(*default_args)