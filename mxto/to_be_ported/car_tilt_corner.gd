class_name MachineTiltCorner extends Resource

enum FZ_TC{
	B1 = 0x1,
	AIRBORNE = 0x2,
	DRIFT = 0x4,
	DISCONNECTED_FROM_TRACK = 0x8,
	STRAFING = 0x10,
	B6 = 0x20,
	B7 = 0x40,
	B8 = 0x80
}

var state : FZ_TC = 0
var offset := Vector3.ZERO
var pos_old := Vector3.ZERO
var pos := Vector3.ZERO
var up_vector := Vector3.ZERO
var up_vector_2 := Vector3.ZERO
var force := 0.0
var rest_length := 0.0
var force_spatial := Vector3.ZERO
var force_spatial_len := 0.0
