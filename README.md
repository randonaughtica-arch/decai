# AI Deco (Geode mod)

Select objects in the Geometry Dash editor, click the cyan **star** button in the right-hand edit column, type what you want ("neon outline with small dots on top", "hanging chains under this platform"), and Claude adds the decoration.

Targets **Geode v5 / GD 2.2081**.

## How it works

1. The selection is turned into a compact description: each object's ID, position (relative to the selection's bottom-left corner), rotation, scale, colors and Z layer.
2. That description plus your prompt goes to the Claude Messages API (`claude-opus-5-5` by default; change it in the mod settings).
3. Claude replies with JSON: a list of objects to add.
4. The mod checks every object (allowed ID, position near the selection, scale within a sane range), builds a GD object string, creates the objects, adds **one undo step**, and selects the new objects.

**Object palette:** the AI may only place object IDs that are already in your selection, plus anything you list under *Extra Object IDs* in the settings (`id:description, id:description`). Any other ID is thrown away, so the AI can't invent objects that don't exist. To get good deco, select the area **and** a few deco pieces in the style you want.

## Setup

1. Get an API key at https://console.anthropic.com.
2. In-game: Geode → AI Deco → Settings → paste the key.

## Building

### Easiest: GitHub Actions (no local tools needed)
Push this folder to a GitHub repo. The workflow in `.github/workflows/multi-platform.yml` builds Windows, macOS and Android. Download the `.geode` file from the run's **Build Output** artifact and put it in `Geometry Dash/geode/mods`.

### Locally (Windows)
Install Visual Studio 2022 (C++ workload), CMake, and the Geode CLI, then:

```
geode sdk install
geode sdk install-binaries
cmake -B build -A x64
cmake --build build --config RelWithDebInfo
```

The built `.geode` file is installed into your GD mods folder automatically.

## Files

- `src/main.cpp`: hooks `EditorUI::init` to add the button
- `src/AIDecoPopup.cpp`: prompt popup, API request, applying the result
- `src/DecoGen.cpp`: selection → prompt, AI reply → GD object string
- `mod.json`: metadata and settings. Change `id` and `developer` to your own name before publishing.
