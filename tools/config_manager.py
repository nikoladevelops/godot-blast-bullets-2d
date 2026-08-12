import json

from paths import CONFIG_PATH


class _PluginConfig:
    def __init__(self, config_path=CONFIG_PATH):
        self.config_path = config_path

        self.data = {
            "pluginName": "plugin_name",
            "godotVersion": "4.7",
            "godotActivePath": "",
            "savedGodotPaths": [],
            "godotProjectFolder": "",
            "ltoMode": "none",
            "selectedBuildProfile": "build_profile.json",
            "extensionApiPath": "godot-cpp/gdextension/extension_api.json",
            "reloadable": True,
            "editorTargetMode": "debug",
            "debugSymbols": "no"
        }

        if self.config_path.exists():
            self._read_config()
        else:
            self._save_config()

    def _read_config(self) -> None:
        with open(self.config_path, "r", encoding="utf-8") as f:
            self.data = json.load(f)

    def _save_config(self) -> None:
        with open(self.config_path, "w", encoding="utf-8") as f:
            json.dump(self.data, f, indent=2)

    def reload(self) -> None:
        """Reload configuration from disk into memory."""
        if self.config_path.exists():
            self._read_config()

    def getGodotVersion(self) -> str:
        self.reload()
        return self.data.get("godotVersion", "4.7")

    def setGodotVersion(self, new_version: str) -> None:
        self.data["godotVersion"] = new_version
        self._save_config()

    def getPluginName(self) -> str:
        self.reload()
        return self.data.get("pluginName", "plugin_name")

    def setPluginName(self, new_name: str) -> None:
        self.data["pluginName"] = new_name
        self._save_config()

    def getGodotProjectFolder(self) -> str:
        self.reload()
        return self.data.get("godotProjectFolder", "test_project")

    def setGodotProjectFolder(self, new_folder: str) -> None:
        self.data["godotProjectFolder"] = new_folder
        self._save_config()

    def getLtoMode(self) -> str:
        self.reload()
        return self.data.get("ltoMode", "none")

    def setLtoMode(self, new_lto: str) -> None:
        self.data["ltoMode"] = new_lto
        self._save_config()

    def getSelectedBuildProfile(self) -> str:
        self.reload()
        return self.data.get("selectedBuildProfile", "")

    def setSelectedBuildProfile(self, new_profile: str) -> None:
        self.data["selectedBuildProfile"] = new_profile
        self._save_config()

    def getExtensionApiPath(self) -> str:
        self.reload()
        return self.data.get("extensionApiPath", "")

    def setExtensionApiPath(self, new_api_path: str) -> None:
        self.data["extensionApiPath"] = str(new_api_path)
        self._save_config()

    def getReloadable(self) -> bool:
        self.reload()
        return self.data.get("reloadable", True)

    def setReloadable(self, is_reloadable: bool) -> None:
        self.data["reloadable"] = is_reloadable
        self._save_config()

    def getEditorTargetMode(self) -> str:
        self.reload()
        return self.data.get("editorTargetMode", "debug")

    def setEditorTargetMode(self, mode: str) -> None:
        self.data["editorTargetMode"] = mode
        self._save_config()

    def getGodotActivePath(self) -> str:
        """Returns the active Godot engine executable path."""
        self.reload()
        return self.data.get("godotActivePath", "")

    def setGodotActivePath(self, new_path: str) -> None:
        """Sets the active godotActivePath in configuration."""
        self.data["godotActivePath"] = str(new_path).strip()
        self._save_config()

    def getSavedGodotPaths(self) -> list[dict[str, str]]:
        """Returns the saved Godot path dictionaries: [{'path': '...', 'version': '...'}, ...]"""
        self.reload()
        return self.data.get("savedGodotPaths", [])

    def addSavedGodotPath(self, new_path: str, version: str = "Unknown") -> None:
        """Adds or updates a path entry inside savedGodotPaths."""
        self.reload()
        paths = self.data.get("savedGodotPaths", [])
        cleaned_path = str(new_path).strip()

        if not cleaned_path:
            return

        for entry in paths:
            if entry["path"] == cleaned_path:
                entry["version"] = version
                self._save_config()
                return

        paths.append({"path": cleaned_path, "version": version})
        self.data["savedGodotPaths"] = paths
        self._save_config()

    def removeSavedGodotPath(self, path_to_remove: str) -> None:
        """
        Removes a path entry from savedGodotPaths.
        If the path being removed is currently active, clears godotActivePath to "".
        """
        self.reload()
        cleaned_remove = str(path_to_remove).strip()
        paths = self.data.get("savedGodotPaths", [])

        new_paths = [p for p in paths if p.get("path") != cleaned_remove]
        self.data["savedGodotPaths"] = new_paths

        if self.data.get("godotActivePath") == cleaned_remove:
            self.data["godotActivePath"] = ""

        self._save_config()

    def getDebugSymbols(self) -> str:
            self.reload()
            return self.data.get("debugSymbols", "no")

    def setDebugSymbols(self, mode: str) -> None:
        self.data["debugSymbols"] = mode.strip().lower()
        self._save_config()

config = _PluginConfig()
