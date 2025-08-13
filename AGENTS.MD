# AGENTS.MD

## Project Structure

- **Godot Project and GDScript**  
	Located in: `/mxto`  
	This folder contains the Godot project and GDScript files. These are primarily for game logic and UI and are not directly involved in the physics simulation.

- **C++ Physics Simulation**  
	Located in: `/src`  
	The core physics simulation code resides here.

	- `src/car` – Code specific to the car's physics and control.
	- `src/track` – Code handling racetrack geometry, collision, and other track-related logic.
	- `src/mxt_core` – Shared utility and core logic used throughout the simulation codebase.

## Development Requirements

- **godot-cpp submodule**  
	To compile this project, you must use the `4.4` branch of the [godot-cpp submodule](https://github.com/godotengine/godot-cpp.git).

## Coding Guidelines

- **Performance First**  
	Always prioritize high-performance patterns:
	- Minimize memory allocations - allocate once and re-use. Scratchpads are your friend.
	- Optimize data layout and cache coherence.
	- Use SIMD instructions where applicable.
	- Avoid unnecessary abstraction or virtual calls in tight loops.

## Runtime Environment

- You have **full, unrestricted internet access**.


## Build Instructions (for agents and humans)

- Preferred build command (fast incremental):
  scons target=template_debug debug_symbols=yes -j4

- Notes:
  - This is the command the project typically uses; please default to it in automation and when testing locally.
  - The default `scons` without arguments can take significantly longer due to full library rebuilds.

## Local patch tool (apply_patch shim)

- A helper script is available at `scripts/apply_patch.sh` to apply patch blocks the same way our agent uses them in this repo.
- It works best inside the Codex CLI environment where the native `apply_patch` command is available. The script forwards the patch block to that command.
- If the native tool is unavailable, and the input is a standard unified diff, it falls back to `git apply`. Otherwise it will stop with a helpful error.

Usage examples
- Apply a Codex-style patch block via here-doc:

  scripts/apply_patch.sh << 'PATCH'
  *** Begin Patch
  *** Update File: path/to/file.ext
  @@
  -old line
  +new line
  *** End Patch
  PATCH

- Apply a standard unified diff:

  git diff > /tmp/changes.diff
  scripts/apply_patch.sh < /tmp/changes.diff

Notes
- This shim does not itself implement a patch engine; it delegates to the Codex CLI `apply_patch` when present, or to `git apply` for standard diffs.
- For complex or large edits, continue to prefer small, focused patches and let the agent or `git` manage conflicts.
