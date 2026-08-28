#pragma once

#include "godot_cpp/classes/array_mesh.hpp"

class RaceTrack;

godot::Ref<godot::ArrayMesh> build_track_minimap_mesh(const RaceTrack& track);
