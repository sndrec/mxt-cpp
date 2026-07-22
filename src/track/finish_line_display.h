#pragma once

class RaceTrack;

namespace godot {

class MeshInstance3D;
class Node;
class Node3D;

class FinishLineDisplay
{
	Node3D* root = nullptr;
	MeshInstance3D* corner_1 = nullptr;
	MeshInstance3D* corner_2 = nullptr;
	MeshInstance3D* plane = nullptr;
	const RaceTrack* configured_track = nullptr;

	void create(Node* parent);

public:
	void configure(Node* parent, const RaceTrack& track);
	void hide();
};

}
