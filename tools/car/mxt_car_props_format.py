from __future__ import annotations

from dataclasses import dataclass, field
import json
import math
import struct
import zlib


MAGIC = b"MXTCPRP\0"
SCHEMA_FINGERPRINT = 0xF201716EAB2F6CEE

STAT_NAMES = (
    "weight_kg",
    "acceleration",
    "max_speed",
    "grip_1",
    "grip_2",
    "grip_3",
    "turn_tension",
    "drift_accel",
    "turn_movement",
    "strafe_turn",
    "strafe",
    "turn_reaction",
    "turn_decel",
    "drag",
    "body",
    "camera_reorienting",
    "camera_repositioning",
    "track_collision",
    "obstacle_collision",
    "max_energy",
    "boost_energy_use_rate",
    "energy_recharge_rate",
    "accel_press_grip_frames",
    "manual_turbo_gain",
    "dashplate_turbo_gain",
    "jumpplate_turbo_gain",
    "dashplate_turbo_heat_multiplier",
    "turbo_flat_loss_per_second",
    "turbo_percent_loss_per_second",
    "turbo_top_speed_effect",
    "manual_boost_duration_seconds",
    "dashplate_boost_duration_seconds",
    "s_boost_base_speed_add_per_second",
    "shift_boost_base_speed_add",
    "shift_boost_velocity_multiplier",
    "air_pitch_up_speed_loss_factor",
    "air_glide_steering_speed_loss_factor",
    "drive_target_speed_multiplier",
    "acceleration_response_multiplier",
    "forward_thrust_multiplier",
)

STAT_IDS = {name: index for index, name in enumerate(STAT_NAMES)}

LAYER_NAMES = (
    "base",
    "mts",
    "quickturn",
    "no_boost",
    "manual_boost",
    "dashplate_boost",
    "stacked_boost",
)

LAYER_IDS = {name: index for index, name in enumerate(LAYER_NAMES)}
MODIFIER_LAYER_NAMES = LAYER_NAMES[1:]
LIVE_MODIFIER_EXCLUSIONS = frozenset(("weight_kg", "max_energy"))
LIVE_MODIFIER_STATS = tuple(name for name in STAT_NAMES if name not in LIVE_MODIFIER_EXCLUSIONS)

DEFAULT_BASE_VALUES = dict(zip(STAT_NAMES, (
    1260.0, 0.45, 0.1, 0.47, 0.7, 0.2, 0.12, 0.4, 145.0, 20.0,
    35.0, 10.0, 0.02, 0.01, 0.85, 1.0, 1.0, 1.3, 2.4, 100.0,
    1.0, 1.0, 1.0, 4.5486, 9.0972, 0.0, 0.2, 6.14061, 0.05117175,
    3.0, 1.5, 0.75, 1.5, 2.0, 1.4, 0.005, 0.012, 1.0, 1.0, 1.0,
)))


@dataclass(slots=True)
class CurveKey:
    time: float
    value: float
    tangent_in: float = 0.0
    tangent_out: float = 0.0


@dataclass(slots=True)
class Curve:
    keys: list[CurveKey] = field(default_factory=list)

    @staticmethod
    def constant(value: float) -> "Curve":
        return Curve([CurveKey(0.0, float(value))])

    def sample(self, machine_setting: float) -> float:
        validate_curve(self)
        if len(self.keys) == 1:
            return self.keys[0].value
        t = min(max(float(machine_setting), 0.0), 1.0)
        if t <= self.keys[0].time:
            return self.keys[0].value
        if t >= self.keys[-1].time:
            return self.keys[-1].value
        for left, right in zip(self.keys, self.keys[1:]):
            if t > right.time:
                continue
            dt = right.time - left.time
            u = (t - left.time) / dt
            h0 = left.value + dt * left.tangent_out / 3.0
            h1 = right.value - dt * right.tangent_in / 3.0
            omt = 1.0 - u
            return (
                left.value * omt * omt * omt
                + 3.0 * h0 * omt * omt * u
                + 3.0 * h1 * omt * u * u
                + right.value * u * u * u
            )
        return self.keys[-1].value

    def derivative(self, machine_setting: float) -> float:
        """Return dy/d(machine setting), using the same cubic as sample()."""
        validate_curve(self)
        if len(self.keys) == 1:
            return 0.0
        t = min(max(float(machine_setting), 0.0), 1.0)
        if t <= self.keys[0].time:
            return self.keys[0].tangent_out
        if t >= self.keys[-1].time:
            return self.keys[-1].tangent_in
        for left, right in zip(self.keys, self.keys[1:]):
            if t > right.time:
                continue
            dt = right.time - left.time
            u = (t - left.time) / dt
            h0 = left.value + dt * left.tangent_out / 3.0
            h1 = right.value - dt * right.tangent_in / 3.0
            omt = 1.0 - u
            derivative_u = 3.0 * (
                (h0 - left.value) * omt * omt
                + 2.0 * (h1 - h0) * omt * u
                + (right.value - h1) * u * u
            )
            return derivative_u / dt
        return self.keys[-1].tangent_in


@dataclass(slots=True)
class CarProperties:
    state_flags: int = 0
    tilt_corners: list[tuple[float, float, float]] = field(default_factory=list)
    wall_corners: list[tuple[float, float, float]] = field(default_factory=list)
    s_boost_overrides: dict[str, float] = field(default_factory=dict)
    curves: dict[str, dict[str, Curve]] = field(default_factory=dict)


def default_properties() -> CarProperties:
    base = {name: Curve.constant(DEFAULT_BASE_VALUES[name]) for name in STAT_NAMES}
    modifiers = {
        layer: {name: Curve.constant(1.0) for name in LIVE_MODIFIER_STATS}
        for layer in MODIFIER_LAYER_NAMES
    }
    return CarProperties(
        state_flags=0,
        tilt_corners=[(-0.8, 0.0, -1.2), (0.8, 0.0, -1.2),
                      (-0.8, 0.0, 1.2), (0.8, 0.0, 1.2)],
        wall_corners=[(-1.0, -0.1, -1.4), (1.0, -0.1, -1.4),
                      (-1.0, -0.1, 1.4), (1.0, -0.1, 1.4)],
        s_boost_overrides={name: DEFAULT_BASE_VALUES[name] for name in LIVE_MODIFIER_STATS},
        curves={"base": base, **modifiers},
    )


def validate_curve(curve: Curve) -> None:
    if not curve.keys:
        raise ValueError("curve has no keys")
    previous_time = -1.0
    for key in curve.keys:
        values = (key.time, key.value, key.tangent_in, key.tangent_out)
        if not all(math.isfinite(value) for value in values):
            raise ValueError("curve contains a non-finite key value")
        if key.time < 0.0 or key.time > 1.0:
            raise ValueError("curve key time is outside [0, 1]")
        if key.time <= previous_time:
            raise ValueError("curve key times are not strictly increasing")
        previous_time = key.time


def validate_properties(properties: CarProperties) -> None:
    if len(properties.tilt_corners) != 4 or len(properties.wall_corners) != 4:
        raise ValueError("exactly four tilt and four wall corners are required")
    for corner in properties.tilt_corners + properties.wall_corners:
        if len(corner) != 3 or not all(math.isfinite(value) for value in corner):
            raise ValueError("collision corner is malformed")
    if set(properties.s_boost_overrides) != set(LIVE_MODIFIER_STATS):
        raise ValueError("S-BOOST override set does not match the schema")
    for name, value in properties.s_boost_overrides.items():
        if name not in LIVE_MODIFIER_STATS or not math.isfinite(value):
            raise ValueError(f"invalid S-BOOST override: {name}")
    if set(properties.curves) != set(LAYER_NAMES):
        raise ValueError("curve layer set does not match the schema")
    for layer_name in LAYER_NAMES:
        required = STAT_NAMES if layer_name == "base" else LIVE_MODIFIER_STATS
        layer = properties.curves[layer_name]
        if set(layer) != set(required):
            raise ValueError(f"curve set for {layer_name} does not match the schema")
        for curve in layer.values():
            validate_curve(curve)


def property_warnings(properties: CarProperties) -> list[str]:
    """Return authoring warnings for legal but suspicious values."""
    validate_properties(properties)
    warnings: list[str] = []
    samples = tuple(index / 100.0 for index in range(101))

    weight_values = [properties.curves["base"]["weight_kg"].sample(t) for t in samples]
    if min(weight_values) <= 0.0:
        warnings.append("weight_kg becomes nonpositive")

    nonnegative_stats = (
        "manual_boost_duration_seconds", "dashplate_boost_duration_seconds",
        "turbo_flat_loss_per_second", "turbo_percent_loss_per_second",
    )
    for name in nonnegative_stats:
        if min(properties.curves["base"][name].sample(t) for t in samples) < 0.0:
            warnings.append(f"{name} becomes negative")

    grip_curve = properties.curves["base"]["accel_press_grip_frames"]
    if any(abs(grip_curve.sample(t) - round(grip_curve.sample(t))) > 0.001 for t in samples):
        warnings.append("accel_press_grip_frames samples to nonintegral values")

    for layer_name in MODIFIER_LAYER_NAMES:
        for stat_name, curve in properties.curves[layer_name].items():
            values = [curve.sample(t) for t in samples]
            if min(values) < -10.0 or max(values) > 10.0:
                warnings.append(f"{layer_name}/{stat_name} has an extreme multiplier")

    for layer_name, layer in properties.curves.items():
        for stat_name, curve in layer.items():
            if len(curve.keys) < 2:
                continue
            for left, right in zip(curve.keys, curve.keys[1:]):
                endpoint_min = min(left.value, right.value)
                endpoint_max = max(left.value, right.value)
                span = max(abs(endpoint_max - endpoint_min), abs(endpoint_min), abs(endpoint_max), 1.0)
                segment_values = [
                    curve.sample(left.time + (right.time - left.time) * index / 16.0)
                    for index in range(17)
                ]
                margin = span * 0.25
                if min(segment_values) < endpoint_min - margin or max(segment_values) > endpoint_max + margin:
                    warnings.append(f"{layer_name}/{stat_name} has suspicious cubic overshoot")
                    break
    return warnings


def write_binary(properties: CarProperties) -> bytes:
    validate_properties(properties)
    payload = bytearray()
    payload += struct.pack("<I", properties.state_flags & 0xFFFFFFFF)
    for corner in properties.tilt_corners + properties.wall_corners:
        payload += struct.pack("<3f", *corner)

    payload += struct.pack("<H", len(LIVE_MODIFIER_STATS))
    for stat_name in LIVE_MODIFIER_STATS:
        payload += struct.pack("<Hf", STAT_IDS[stat_name], properties.s_boost_overrides[stat_name])

    curve_count = len(STAT_NAMES) + len(MODIFIER_LAYER_NAMES) * len(LIVE_MODIFIER_STATS)
    payload += struct.pack("<H", curve_count)
    for layer_name in LAYER_NAMES:
        stat_names = STAT_NAMES if layer_name == "base" else LIVE_MODIFIER_STATS
        for stat_name in stat_names:
            curve = properties.curves[layer_name][stat_name]
            payload += struct.pack("<HBBH", STAT_IDS[stat_name], LAYER_IDS[layer_name], 0, len(curve.keys))
            if len(curve.keys) == 1:
                payload += struct.pack("<f", curve.keys[0].value)
            else:
                for key in curve.keys:
                    payload += struct.pack("<4f", key.time, key.value, key.tangent_in, key.tangent_out)

    header = struct.pack(
        "<8sQII",
        MAGIC,
        SCHEMA_FINGERPRINT,
        len(payload),
        zlib.crc32(payload) & 0xFFFFFFFF,
    )
    return header + payload


def read_binary(data: bytes) -> CarProperties:
    if len(data) < 24:
        raise ValueError("car properties file is shorter than its header")
    magic, fingerprint, payload_size, expected_crc = struct.unpack_from("<8sQII", data, 0)
    if magic != MAGIC:
        raise ValueError("car properties magic does not match")
    if fingerprint != SCHEMA_FINGERPRINT:
        raise ValueError("car properties schema fingerprint does not match")
    payload = data[24:]
    if payload_size != len(payload):
        raise ValueError("car properties payload size does not match")
    if zlib.crc32(payload) & 0xFFFFFFFF != expected_crc:
        raise ValueError("car properties payload CRC does not match")

    cursor = 0

    def unpack(fmt: str):
        nonlocal cursor
        size = struct.calcsize(fmt)
        if cursor + size > len(payload):
            raise ValueError("car properties payload is truncated")
        values = struct.unpack_from(fmt, payload, cursor)
        cursor += size
        return values

    state_flags = unpack("<I")[0]
    corners = [unpack("<3f") for _ in range(8)]
    override_count = unpack("<H")[0]
    overrides: dict[str, float] = {}
    for _ in range(override_count):
        stat_id, value = unpack("<Hf")
        if stat_id >= len(STAT_NAMES):
            raise ValueError("S-BOOST override has an unknown stat ID")
        name = STAT_NAMES[stat_id]
        if name in overrides:
            raise ValueError("S-BOOST override is duplicated")
        overrides[name] = value

    curves = {layer_name: {} for layer_name in LAYER_NAMES}
    curve_count = unpack("<H")[0]
    for _ in range(curve_count):
        stat_id, layer_id, reserved, key_count = unpack("<HBBH")
        if stat_id >= len(STAT_NAMES) or layer_id >= len(LAYER_NAMES) or reserved != 0:
            raise ValueError("curve header is invalid")
        if key_count == 0:
            raise ValueError("curve has no keys")
        stat_name = STAT_NAMES[stat_id]
        layer_name = LAYER_NAMES[layer_id]
        if stat_name in curves[layer_name]:
            raise ValueError("curve stat/layer pair is duplicated")
        if key_count == 1:
            curve = Curve.constant(unpack("<f")[0])
        else:
            curve = Curve([CurveKey(*unpack("<4f")) for _ in range(key_count)])
        curves[layer_name][stat_name] = curve

    if cursor != len(payload):
        raise ValueError("car properties payload contains trailing bytes")
    properties = CarProperties(
        state_flags=state_flags,
        tilt_corners=[tuple(corner) for corner in corners[:4]],
        wall_corners=[tuple(corner) for corner in corners[4:]],
        s_boost_overrides=overrides,
        curves=curves,
    )
    validate_properties(properties)
    return properties


def properties_to_json(properties: CarProperties, indent: int = 2) -> str:
    validate_properties(properties)

    def curve_dict(curve: Curve) -> list[dict[str, float]]:
        return [
            {
                "time": key.time,
                "value": key.value,
                "tangent_in": key.tangent_in,
                "tangent_out": key.tangent_out,
            }
            for key in curve.keys
        ]

    value = {
        "schema_fingerprint": f"0x{SCHEMA_FINGERPRINT:016x}",
        "state_flags": properties.state_flags,
        "tilt_corners": properties.tilt_corners,
        "wall_corners": properties.wall_corners,
        "s_boost_overrides": properties.s_boost_overrides,
        "curves": {
            layer_name: {
                stat_name: curve_dict(curve)
                for stat_name, curve in properties.curves[layer_name].items()
            }
            for layer_name in LAYER_NAMES
        },
    }
    return json.dumps(value, indent=indent, sort_keys=False)


def properties_from_json(text: str) -> CarProperties:
    value = json.loads(text)
    fingerprint = int(str(value["schema_fingerprint"]), 0)
    if fingerprint != SCHEMA_FINGERPRINT:
        raise ValueError("JSON schema fingerprint does not match")
    curves = {}
    for layer_name, layer in value["curves"].items():
        curves[layer_name] = {
            stat_name: Curve([CurveKey(**key) for key in keys])
            for stat_name, keys in layer.items()
        }
    properties = CarProperties(
        state_flags=int(value["state_flags"]),
        tilt_corners=[tuple(map(float, corner)) for corner in value["tilt_corners"]],
        wall_corners=[tuple(map(float, corner)) for corner in value["wall_corners"]],
        s_boost_overrides={name: float(v) for name, v in value["s_boost_overrides"].items()},
        curves=curves,
    )
    validate_properties(properties)
    return properties
