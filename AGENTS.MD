# AGENTS.MD

## Project Structure

- **Godot Project and GDScript**  
	Located in: `/mxto`  
	This folder contains the Godot project and GDScript files. These are primarily for game logic, rendering, and UI. GDScript is responsible for passing player data into C++ such as car stats, player info, etc... but they are not directly involved in the physics simulation.

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
- **Code like you're writing C!**
  - Avoid doing anything that isn't possible to do in plain C unless it is absolutely strictly necessary.
  - If you are going to code something that is only possible in C++, you must justify it.

## Runtime Environment

- You have **full, unrestricted internet access**.


## Build Instructions (for agents and humans)

- Preferred build command (fast incremental):
  scons target=template_debug debug_symbols=yes -j4

- Notes:
  - This is the command the project typically uses; please default to it in automation and when testing locally.
  - The default `scons` without arguments can take significantly longer due to full library rebuilds.
