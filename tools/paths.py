from pathlib import Path

# Absolute path to the directory containing this script
TOOLS_DIR = Path(__file__).resolve().parent

# Absolute path to the project root (one directory up from tools)
PROJECT_ROOT = TOOLS_DIR.parent

# This is where the config json lives
CONFIG_PATH = TOOLS_DIR / "config.json"

# This is where godot-cpp submodule is
SUBMODULE_PATH = PROJECT_ROOT / "godot-cpp"

# This is where all extension_api json files are located
GDEXTENSION_APIS_PATH = SUBMODULE_PATH / "gdextension"

# This is where you put icons for your nodes and they automatically get transferred to your godot project
ICONS_SOURCE_DIR = PROJECT_ROOT / "icons"

# This is where the build profiles are stored
BUILD_PROFILES_DIR = PROJECT_ROOT / "build_profiles"

# This is where the SConstruct file for scons is
SCONSTRUCT_PATH = PROJECT_ROOT / "SConstruct"

# This is where your custom class reference xml files are located for doc tool generation
DOCS_SOURCE_DIR = PROJECT_ROOT / "doc_classes"

# This is where the register_types.cpp is
SRC_DIR = PROJECT_ROOT / "src"


def get_godot_project_dir() -> Path:
    """
    Returns the absolute Path to the active target Godot project directory
    (supports both absolute paths for external projects and relative workspace paths).
    """
    from config_manager import config
    project_str = config.getGodotProjectFolder()

    if not project_str:
        return PROJECT_ROOT / "test_project"

    path_obj = Path(project_str)
    if path_obj.is_absolute():
        return path_obj

    workspace_resolved = PROJECT_ROOT / path_obj
    if workspace_resolved.exists():
        return workspace_resolved

    return path_obj.resolve()


def get_plugin_dir() -> Path:
    """Returns the absolute Path to the plugin folder inside the selected active Godot project."""
    from config_manager import config
    return get_godot_project_dir() / "addons" / config.getPluginName()


def get_gdextension_file_path() -> Path:
    """Returns the absolute Path to the master source .gdextension manifest file in the workspace root template."""
    from config_manager import config
    plugin_name = config.getPluginName()
    return PROJECT_ROOT / f"{plugin_name}.gdextension"


def get_selected_extension_api_path() -> Path:
    """
    Returns the absolute Path to the active extension_api JSON file by combining
    PROJECT_ROOT with the relative path stored in config.
    """
    from config_manager import config
    rel_path_str = config.getExtensionApiPath()
    if rel_path_str:
        return PROJECT_ROOT / rel_path_str

    return GDEXTENSION_APIS_PATH / "extension_api.json"
