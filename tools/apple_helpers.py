def generate_info_plist(libname: str, platform: str, target: str, precision: str, bundle_id_prefix: str) -> str:
    """Generates the Info.plist content XML string for macOS frameworks or iOS xcframeworks."""
    if not target:
        raise ValueError("The build target (template_debug/template_release) must be specified.")

    framework_name = f"lib{libname}.{platform}.{target}.{precision}"
    bundle_id = f"{bundle_id_prefix}.{libname}"

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


def write_info_plist(target_nodes, source_nodes, env, plist_content: str) -> None:
    """SCons command callback wrapper to write the plist content to disk."""
    with open(target_nodes[0].abspath, 'w', encoding="utf-8") as f:
        f.write(plist_content)


def build_apple_framework(env, libname: str, lib_filename: str, precision: str, bundle_id_prefix: str, sources: list, build_target: str) -> tuple:
    """
    Encapsulates all complex macOS framework and iOS xcframework command chaining,
    keeping SConstruct clean.
    Returns a tuple of (library_target, install_source_path).
    """
    if not build_target:
        raise ValueError("The build target parameter is required.")

    platform = env['platform']
    temp_lib = env.SharedLibrary(f"bin/{platform}/{lib_filename}", source=sources)

    if platform == 'macos':
        if env.get('arch') != 'universal':
            env['arch'] = 'universal'
        framework_name = f"lib{libname}.macos.{build_target}.{precision}.framework"
        framework_binary = f"lib{libname}.macos.{build_target}.{precision}"
        framework_dir = f"bin/{platform}/{framework_name}"
        plist_file = f"{framework_dir}/Info.plist"

        # Lambda uses parameters 'target', 'source', 'env' to match SCons keyword invocation,
        # while safely capturing 'build_target' from the outer function scope.
        env.Command(
            plist_file,
            [],
            lambda target, source, env: write_info_plist(
                target, source, env, generate_info_plist(libname, 'macos', build_target, precision, bundle_id_prefix)
            )
        )
        library = env.Command(
            f"{framework_dir}/{framework_binary}",
            temp_lib,
            [
                f"mkdir -p {framework_dir}",
                f"cp $SOURCE $TARGET",
                f"rm -f bin/{platform}/{lib_filename}"
            ]
        )
        env.Depends(library, plist_file)
        return library, framework_dir

    else:  # ios
        if not env.get('arch'):
            env['arch'] = 'arm64'
        temp_fw_name = f"lib{libname}.ios.{build_target}.{precision}.framework"
        framework_binary = f"lib{libname}.ios.{build_target}.{precision}"
        framework_name = f"lib{libname}.ios.{build_target}.{precision}.xcframework"
        temp_fw_dir = f"bin/{platform}/{temp_fw_name}"
        plist_file = f"{temp_fw_dir}/Info.plist"

        env.Command(
            plist_file,
            [],
            lambda target, source, env: write_info_plist(
                target, source, env, generate_info_plist(libname, 'ios', build_target, precision, bundle_id_prefix)
            )
        )
        temp_framework = env.Command(
            f"{temp_fw_dir}/{framework_binary}",
            temp_lib,
            [
                f"mkdir -p {temp_fw_dir}",
                f"cp $SOURCE $TARGET"
            ]
        )
        env.Depends(temp_framework, plist_file)
        library = env.Command(
            f"bin/{platform}/{framework_name}",
            temp_framework,
            [
                f"xcodebuild -create-xcframework -framework {temp_fw_dir} -output $TARGET",
                f"rm -rf {temp_fw_dir}",
                f"rm -f bin/{platform}/{lib_filename}"
            ]
        )
        return library, f"bin/{platform}/{framework_name}"
