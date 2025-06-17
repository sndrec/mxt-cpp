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
	- Minimize memory allocations.
	- Optimize data layout and cache coherence.
	- Use SIMD instructions where applicable.
	- Avoid unnecessary abstraction or virtual calls in tight loops.

## Runtime Environment

- You have **full, unrestricted internet access**.
