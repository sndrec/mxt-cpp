using Godot;
using System;

namespace MaxxThrottle;

[GlobalClass]
public partial class CarBase : Node3D
{
	
	public enum CarState {
		WAITING,
		PLAY,
		FALL,
		DYING,
		DEAD,
		GOAL
	}

	public enum BoostState {
		NONE,
		PAD,
		MANUAL,
		BOTH
	}

	public enum SteerState {
		NORMAL,
		DRIFT,
		QUICK
	}
	
	public override void _Ready()
	{
	}

	public override void _Process(double delta)
	{
	}
}
