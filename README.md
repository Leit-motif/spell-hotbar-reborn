# Spell Hotbar Reborn – MCO BFCO

Source code for the Skyrim Special Edition mod **Spell Hotbar Reborn – MCO BFCO**, published on
Nexus Mods. It is a fork of [Spell Hotbar 2](https://github.com/pWn3d1337/Skyrim_SpellHotbar2) by
pWn3d1337, forked at upstream release 0.0.14.

To play the mod, install the release from its Nexus page. This repository holds the code and what
builds it. The Nexus archive also carries files that are not here: upstream Spell Hotbar 2's
assets, icon art, and animation clips distributed under their authors' permission.

## Layout

| Path | Contents |
|---|---|
| `skse_plugin/` | The SKSE plugin (`SpellHotbar2.dll`): C++23 on CommonLibSSE-NG, with its CMake and vcpkg build files and host tests. |
| `plugin-src/` | `SpellHotbar.esp` and `SpellHotbar_BattleMage.esp` as Spriggit YAML. |
| `python_scripts/build_plugins.py` | Builds both ESPs from `plugin-src/` and verifies each one round-trips. |
| `papyrus/` | `SpellHotbar.psc`, the native-function declarations, and its compiled `.pex`. |
| `nemesis/` | The `shtb` behavior patch for Nemesis or Pandora, and its Behavior Data Injector config. |
| `data/SKSE/Plugins/SpellHotbar/` | The Ability catalogues (`artdata/`) and the translation file (`localization/`). |

## Building the DLL

Requirements: Visual Studio 2022 with the C++ desktop workload, CMake 3.21 or later, Ninja, and
[vcpkg](https://github.com/microsoft/vcpkg) with `VCPKG_ROOT` set to its checkout.

From an x64 Native Tools Command Prompt for VS 2022:

```bat
cd skse_plugin
cmake --preset release
cmake --build build/release
ctest --test-dir build/release
```

The DLL lands in `skse_plugin/build/release/SpellHotbar2.dll`. To copy it into a mod folder after
each build, add `-DOUTPUT_FOLDER=<mod folder>` to the configure step; the DLL then goes to
`<mod folder>/SKSE/Plugins/`.

## Building the ESPs

Requirements: Python 3 and the .NET SDK. The script restores the Spriggit CLI pinned in
`.config/dotnet-tools.json`.

```bat
python python_scripts/build_plugins.py
```

Both ESPs land in `build/plugins/`.

## License

Spell Hotbar 2 is released by pWn3d1337 under the MIT License; its notice is in
`skse_plugin/LICENSE.txt`. This fork is distributed under the same license.
