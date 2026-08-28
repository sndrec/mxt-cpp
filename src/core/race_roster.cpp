#include "core/race_roster.h"

#include "core/bounded_wire.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace godot;

namespace {

static constexpr uint8_t WIRE_MAGIC[8] = {'M', 'X', 'T', 'R', 'S', 'T', 'R', 0};
static constexpr uint8_t WIRE_VERSION = 1;
static constexpr uint32_t MAX_RACERS = 1024;
static constexpr uint32_t MAX_STAMPS = 16;
static constexpr uint32_t MAX_USERNAME_BYTES = 256;
static constexpr uint32_t MAX_FIELD_BYTES = 1024;

enum StampFlag : uint8_t {
	STAMP_CUSTOM = 1u << 0,
	STAMP_RECT_ROTATED = 1u << 1,
	STAMP_ENABLED = 1u << 2,
	STAMP_FLIP_HORIZONTAL = 1u << 3,
	STAMP_FLIP_VERTICAL = 1u << 4,
	STAMP_MIRROR_LOCAL_X = 1u << 5,
};

static bool bounded(const String &value, uint32_t maximum) {
	return static_cast<uint32_t>(value.utf8().length()) <= maximum;
}

static float finite_float(const Variant &value, float fallback) {
	const float converted = static_cast<float>(value);
	return std::isfinite(converted) ? converted : fallback;
}

static Color dictionary_colour(const Dictionary &data, const char *key, const Color &fallback) {
	const Variant value = data.get(key, Variant());
	if (value.get_type() == Variant::COLOR) return value;
	if (value.get_type() == Variant::STRING) return Color::html(value);
	return fallback;
}

static Vector2 array_vector2(const Variant &value, const Vector2 &fallback) {
	if (value.get_type() != Variant::ARRAY) return fallback;
	const Array array = value;
	if (array.size() < 2) return fallback;
	return Vector2(finite_float(array[0], fallback.x), finite_float(array[1], fallback.y));
}

static Vector3 array_vector3(const Variant &value, const Vector3 &fallback) {
	if (value.get_type() != Variant::ARRAY) return fallback;
	const Array array = value;
	if (array.size() < 3) return fallback;
	return Vector3(finite_float(array[0], fallback.x), finite_float(array[1], fallback.y), finite_float(array[2], fallback.z));
}

static Rect2 array_rect2(const Variant &value, const Rect2 &fallback) {
	if (value.get_type() != Variant::ARRAY) return fallback;
	const Array array = value;
	if (array.size() < 4) return fallback;
	return Rect2(
		finite_float(array[0], fallback.position.x), finite_float(array[1], fallback.position.y),
		finite_float(array[2], fallback.size.x), finite_float(array[3], fallback.size.y));
}

static Basis array_basis(const Variant &value, const Basis &fallback) {
	if (value.get_type() != Variant::ARRAY) return fallback;
	const Array array = value;
	if (array.size() < 3) return fallback;
	const Basis out(array_vector3(array[0], fallback.get_column(0)), array_vector3(array[1], fallback.get_column(1)), array_vector3(array[2], fallback.get_column(2)));
	return std::abs(out.determinant()) > 1.0e-8f ? out : fallback;
}

static Array vector2_array(const Vector2 &value) {
	Array out;
	out.push_back(value.x);
	out.push_back(value.y);
	return out;
}

static Array vector3_array(const Vector3 &value) {
	Array out;
	out.push_back(value.x);
	out.push_back(value.y);
	out.push_back(value.z);
	return out;
}

static Array rect2_array(const Rect2 &value) {
	Array out;
	out.push_back(value.position.x);
	out.push_back(value.position.y);
	out.push_back(value.size.x);
	out.push_back(value.size.y);
	return out;
}

static Array basis_array(const Basis &value) {
	Array out;
	out.push_back(vector3_array(value.get_column(0)));
	out.push_back(vector3_array(value.get_column(1)));
	out.push_back(vector3_array(value.get_column(2)));
	return out;
}

static void write_vector2(mxt_wire::Writer &writer, const Vector2 &value) {
	writer.f32(value.x);
	writer.f32(value.y);
}

static void write_vector3(mxt_wire::Writer &writer, const Vector3 &value) {
	writer.f32(value.x);
	writer.f32(value.y);
	writer.f32(value.z);
}

static bool read_vector2(mxt_wire::Reader &reader, Vector2 &value) {
	return reader.f32(value.x) && reader.f32(value.y) && std::isfinite(value.x) && std::isfinite(value.y);
}

static bool read_vector3(mxt_wire::Reader &reader, Vector3 &value) {
	return reader.f32(value.x) && reader.f32(value.y) && reader.f32(value.z) &&
		std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

} // namespace

void MxtRaceRoster::_bind_methods() {
	ClassDB::bind_method(D_METHOD("copy"), &MxtRaceRoster::copy);
	ClassDB::bind_method(D_METHOD("clear"), &MxtRaceRoster::clear);
	ClassDB::bind_method(D_METHOD("count"), &MxtRaceRoster::count);
	ClassDB::bind_method(D_METHOD("append_settings", "player_id", "network_peer_id", "cpu", "bumper", "disconnected", "settings"), &MxtRaceRoster::append_settings);
	ClassDB::bind_method(D_METHOD("upsert_settings", "player_id", "network_peer_id", "cpu", "bumper", "disconnected", "settings"), &MxtRaceRoster::upsert_settings);
	ClassDB::bind_method(D_METHOD("remove_player", "player_id"), &MxtRaceRoster::remove_player);
	ClassDB::bind_method(D_METHOD("find_player", "player_id"), &MxtRaceRoster::find_player);
	ClassDB::bind_method(D_METHOD("get_player_id", "index"), &MxtRaceRoster::get_player_id);
	ClassDB::bind_method(D_METHOD("get_network_peer_id", "index"), &MxtRaceRoster::get_network_peer_id);
	ClassDB::bind_method(D_METHOD("is_cpu", "index"), &MxtRaceRoster::is_cpu);
	ClassDB::bind_method(D_METHOD("is_bumper", "index"), &MxtRaceRoster::is_bumper);
	ClassDB::bind_method(D_METHOD("is_spectator", "index"), &MxtRaceRoster::is_spectator);
	ClassDB::bind_method(D_METHOD("is_disconnected", "index"), &MxtRaceRoster::is_disconnected);
	ClassDB::bind_method(D_METHOD("get_settings_dictionary", "index"), &MxtRaceRoster::get_settings_dictionary);
	ClassDB::bind_method(D_METHOD("get_player_settings_dictionary", "player_id"), &MxtRaceRoster::get_player_settings_dictionary);
	ClassDB::bind_method(D_METHOD("get_last_error"), &MxtRaceRoster::get_last_error);
	ClassDB::bind_method(D_METHOD("encode_wire"), &MxtRaceRoster::encode_wire);
	ClassDB::bind_method(D_METHOD("decode_wire", "bytes"), &MxtRaceRoster::decode_wire);

	BIND_ENUM_CONSTANT(FLAG_CPU);
	BIND_ENUM_CONSTANT(FLAG_BUMPER);
	BIND_ENUM_CONSTANT(FLAG_SPECTATOR);
	BIND_ENUM_CONSTANT(FLAG_DISCONNECTED);
}

Ref<MxtRaceRoster> MxtRaceRoster::copy() const {
	Ref<MxtRaceRoster> out;
	out.instantiate();
	out->entries = entries;
	return out;
}

void MxtRaceRoster::clear() {
	entries.clear();
	last_error = String();
}

bool MxtRaceRoster::parse_entry(int64_t player_id, int64_t network_peer_id, uint8_t flags, const Dictionary &settings, Entry &out) {
	if (player_id <= 0) {
		last_error = "Roster player ID must be positive.";
		return false;
	}
	out = Entry();
	out.player_id = player_id;
	out.network_peer_id = network_peer_id > 0 ? network_peer_id : player_id;
	out.username = settings.get("username", "Player");
	out.vehicle_content_id = settings.get("vehicle_content_id", "");
	out.vehicle_gameplay_digest = settings.get("vehicle_gameplay_digest", "");
	out.vehicle_package_digest = settings.get("vehicle_package_digest", "");
	out.vehicle_workshop_id = settings.get("vehicle_workshop_id", "");
	if (!bounded(out.username, MAX_USERNAME_BYTES) || !bounded(out.vehicle_content_id, MAX_FIELD_BYTES) ||
			!bounded(out.vehicle_gameplay_digest, MAX_FIELD_BYTES) || !bounded(out.vehicle_package_digest, MAX_FIELD_BYTES) ||
			!bounded(out.vehicle_workshop_id, MAX_FIELD_BYTES)) {
		last_error = "Roster settings exceed bounded string limits.";
		return false;
	}
	out.accel_setting = std::clamp(finite_float(settings.get("accel_setting", 1.0f), 1.0f), 0.0f, 1.0f);
	out.stickers[0] = settings.get("sticker_1", 0);
	out.stickers[1] = settings.get("sticker_2", 1);
	out.stickers[2] = settings.get("sticker_3", 2);
	out.stickers[3] = settings.get("sticker_4", 3);
	out.flags = flags & (FLAG_CPU | FLAG_BUMPER | FLAG_DISCONNECTED);
	if (static_cast<bool>(settings.get("spectator", false))) out.flags |= FLAG_SPECTATOR;

	const Variant livery_value = settings.get("car_livery", Variant());
	if (livery_value.get_type() != Variant::DICTIONARY || static_cast<Dictionary>(livery_value).is_empty()) return true;
	const Dictionary livery = livery_value;
	out.flags |= FLAG_HAS_LIVERY;
	out.livery.vehicle_content_id = livery.get("vehicle_content_id", "");
	if (!bounded(out.livery.vehicle_content_id, MAX_FIELD_BYTES)) {
		last_error = "Livery content ID exceeds bounded string limits.";
		return false;
	}
	out.livery.primary_colour = dictionary_colour(livery, "primary_colour", out.livery.primary_colour);
	out.livery.secondary_colour = dictionary_colour(livery, "secondary_colour", out.livery.secondary_colour);
	out.livery.accent_colour = dictionary_colour(livery, "accent_colour", out.livery.accent_colour);
	out.livery.outline_colour = dictionary_colour(livery, "outline_colour", out.livery.outline_colour);
	out.livery.trail_colour = dictionary_colour(livery, "trail_colour", out.livery.trail_colour);
	out.livery.outline_colour_customized = livery.get("outline_colour_customized", false);
	out.livery.trail_colour_customized = livery.get("trail_colour_customized", false);
	const Variant stamps_value = livery.get("stamps", Array());
	if (stamps_value.get_type() != Variant::ARRAY) {
		last_error = "Livery stamps must be an array.";
		return false;
	}
	const Array stamps = stamps_value;
	if (stamps.size() > MAX_STAMPS) {
		last_error = "Livery exceeds the 16-stamp limit.";
		return false;
	}
	out.livery.stamps.reserve(stamps.size());
	for (int64_t i = 0; i < stamps.size(); ++i) {
		if (stamps[i].get_type() != Variant::DICTIONARY) {
			last_error = "Livery stamp is not an object.";
			return false;
		}
		const Dictionary data = stamps[i];
		Stamp stamp;
		stamp.stamp_id = data.get("stamp_id", "");
		stamp.custom_hash = data.get("hash", "");
		if (!bounded(stamp.stamp_id, MAX_FIELD_BYTES) || !bounded(stamp.custom_hash, MAX_FIELD_BYTES)) {
			last_error = "Livery stamp identifier exceeds bounded string limits.";
			return false;
		}
		if (String(data.get("source", "base")) == "custom") stamp.flags |= STAMP_CUSTOM;
		if (static_cast<bool>(data.get("rect_rotated", false))) stamp.flags |= STAMP_RECT_ROTATED;
		if (static_cast<bool>(data.get("enabled", true))) stamp.flags |= STAMP_ENABLED;
		if (static_cast<bool>(data.get("flip_horizontal", false))) stamp.flags |= STAMP_FLIP_HORIZONTAL;
		if (static_cast<bool>(data.get("flip_vertical", false))) stamp.flags |= STAMP_FLIP_VERTICAL;
		if (static_cast<bool>(data.get("mirror_local_x", false))) stamp.flags |= STAMP_MIRROR_LOCAL_X;
		stamp.palette_id = std::max(0, static_cast<int32_t>(data.get("palette_id", 0)));
		stamp.layer = data.get("layer", 0);
		stamp.custom_rect = array_rect2(data.get("rect", Array()), Rect2());
		stamp.custom_rect.size.x = std::max(0.0f, stamp.custom_rect.size.x);
		stamp.custom_rect.size.y = std::max(0.0f, stamp.custom_rect.size.y);
		stamp.local_origin = array_vector3(data.get("local_origin", Array()), Vector3());
		stamp.local_basis = array_basis(data.get("local_basis", Array()), Basis());
		stamp.rotation = finite_float(data.get("rotation", 0.0f), 0.0f);
		stamp.size = array_vector2(data.get("size", Array()), Vector2(1.0f, 1.0f));
		stamp.size.x = std::max(0.001f, stamp.size.x);
		stamp.size.y = std::max(0.001f, stamp.size.y);
		stamp.projection_depth = std::max(0.001f, finite_float(data.get("projection_depth", 0.25f), 0.25f));
		stamp.colour = dictionary_colour(data, "colour", Color(1.0f, 1.0f, 1.0f, 1.0f));
		stamp.opacity = std::clamp(finite_float(data.get("opacity", 1.0f), 1.0f), 0.0f, 1.0f);
		out.livery.stamps.push_back(stamp);
	}
	return true;
}

bool MxtRaceRoster::append_settings(int64_t player_id, int64_t network_peer_id, bool cpu, bool bumper, bool disconnected, const Dictionary &settings) {
	if (entries.size() >= MAX_RACERS || find_player(player_id) >= 0) {
		last_error = entries.size() >= MAX_RACERS ? "Roster exceeds the 1024-racer limit." : "Roster already contains this player ID.";
		return false;
	}
	Entry entry;
	const uint8_t flags = (cpu ? FLAG_CPU : 0) | (bumper ? FLAG_BUMPER : 0) | (disconnected ? FLAG_DISCONNECTED : 0);
	if (!parse_entry(player_id, network_peer_id, flags, settings, entry)) return false;
	entries.push_back(std::move(entry));
	last_error = String();
	return true;
}

bool MxtRaceRoster::upsert_settings(int64_t player_id, int64_t network_peer_id, bool cpu, bool bumper, bool disconnected, const Dictionary &settings) {
	Entry entry;
	const uint8_t flags = (cpu ? FLAG_CPU : 0) | (bumper ? FLAG_BUMPER : 0) | (disconnected ? FLAG_DISCONNECTED : 0);
	if (!parse_entry(player_id, network_peer_id, flags, settings, entry)) return false;
	const int32_t index = find_player(player_id);
	if (index >= 0) entries[static_cast<size_t>(index)] = std::move(entry);
	else {
		if (entries.size() >= MAX_RACERS) {
			last_error = "Roster exceeds the 1024-racer limit.";
			return false;
		}
		entries.push_back(std::move(entry));
	}
	last_error = String();
	return true;
}

bool MxtRaceRoster::remove_player(int64_t player_id) {
	const int32_t index = find_player(player_id);
	if (index < 0) return false;
	entries.erase(entries.begin() + index);
	return true;
}

int32_t MxtRaceRoster::find_player(int64_t player_id) const {
	for (size_t i = 0; i < entries.size(); ++i) if (entries[i].player_id == player_id) return static_cast<int32_t>(i);
	return -1;
}

int64_t MxtRaceRoster::get_player_id(int32_t index) const {
	return index >= 0 && static_cast<size_t>(index) < entries.size() ? entries[index].player_id : 0;
}

int64_t MxtRaceRoster::get_network_peer_id(int32_t index) const {
	return index >= 0 && static_cast<size_t>(index) < entries.size() ? entries[index].network_peer_id : 0;
}

bool MxtRaceRoster::is_cpu(int32_t index) const { return index >= 0 && static_cast<size_t>(index) < entries.size() && (entries[index].flags & FLAG_CPU); }
bool MxtRaceRoster::is_bumper(int32_t index) const { return index >= 0 && static_cast<size_t>(index) < entries.size() && (entries[index].flags & FLAG_BUMPER); }
bool MxtRaceRoster::is_spectator(int32_t index) const { return index >= 0 && static_cast<size_t>(index) < entries.size() && (entries[index].flags & FLAG_SPECTATOR); }
bool MxtRaceRoster::is_disconnected(int32_t index) const { return index >= 0 && static_cast<size_t>(index) < entries.size() && (entries[index].flags & FLAG_DISCONNECTED); }

Dictionary MxtRaceRoster::entry_dictionary(const Entry &entry) const {
	Dictionary out;
	out["username"] = entry.username;
	out["vehicle_content_id"] = entry.vehicle_content_id;
	out["vehicle_gameplay_digest"] = entry.vehicle_gameplay_digest;
	out["vehicle_package_digest"] = entry.vehicle_package_digest;
	out["vehicle_workshop_id"] = entry.vehicle_workshop_id;
	out["accel_setting"] = entry.accel_setting;
	out["spectator"] = (entry.flags & FLAG_SPECTATOR) != 0;
	out["sticker_1"] = entry.stickers[0];
	out["sticker_2"] = entry.stickers[1];
	out["sticker_3"] = entry.stickers[2];
	out["sticker_4"] = entry.stickers[3];
	Dictionary livery;
	if (entry.flags & FLAG_HAS_LIVERY) {
		livery["version"] = 1;
		livery["vehicle_content_id"] = entry.livery.vehicle_content_id;
		livery["primary_colour"] = entry.livery.primary_colour.to_html();
		livery["secondary_colour"] = entry.livery.secondary_colour.to_html();
		livery["accent_colour"] = entry.livery.accent_colour.to_html();
		livery["outline_colour"] = entry.livery.outline_colour.to_html();
		livery["trail_colour"] = entry.livery.trail_colour.to_html();
		livery["outline_colour_customized"] = entry.livery.outline_colour_customized;
		livery["trail_colour_customized"] = entry.livery.trail_colour_customized;
		Array stamps;
		stamps.resize(entry.livery.stamps.size());
		for (size_t i = 0; i < entry.livery.stamps.size(); ++i) {
			const Stamp &stamp = entry.livery.stamps[i];
			Dictionary data;
			data["stamp_id"] = stamp.stamp_id;
			data["source"] = (stamp.flags & STAMP_CUSTOM) ? "custom" : "base";
			data["hash"] = stamp.custom_hash;
			data["palette_id"] = stamp.palette_id;
			data["rect"] = rect2_array(stamp.custom_rect);
			data["rect_rotated"] = (stamp.flags & STAMP_RECT_ROTATED) != 0;
			data["enabled"] = (stamp.flags & STAMP_ENABLED) != 0;
			data["layer"] = stamp.layer;
			data["local_origin"] = vector3_array(stamp.local_origin);
			data["local_basis"] = basis_array(stamp.local_basis);
			data["rotation"] = stamp.rotation;
			data["size"] = vector2_array(stamp.size);
			data["flip_horizontal"] = (stamp.flags & STAMP_FLIP_HORIZONTAL) != 0;
			data["flip_vertical"] = (stamp.flags & STAMP_FLIP_VERTICAL) != 0;
			data["mirror_local_x"] = (stamp.flags & STAMP_MIRROR_LOCAL_X) != 0;
			data["projection_depth"] = stamp.projection_depth;
			data["colour"] = stamp.colour.to_html();
			data["opacity"] = stamp.opacity;
			stamps[i] = data;
		}
		livery["stamps"] = stamps;
	}
	out["car_livery"] = livery;
	return out;
}

Dictionary MxtRaceRoster::get_settings_dictionary(int32_t index) const {
	return index >= 0 && static_cast<size_t>(index) < entries.size() ? entry_dictionary(entries[index]) : Dictionary();
}

Dictionary MxtRaceRoster::get_player_settings_dictionary(int64_t player_id) const {
	return get_settings_dictionary(find_player(player_id));
}

PackedByteArray MxtRaceRoster::encode_wire() const {
	mxt_wire::Writer writer;
	writer.raw(WIRE_MAGIC, sizeof(WIRE_MAGIC));
	writer.u8(WIRE_VERSION);
	writer.u16(static_cast<uint16_t>(entries.size()));
	for (const Entry &entry : entries) {
		writer.i64(entry.player_id);
		writer.i64(entry.network_peer_id);
		writer.u8(entry.flags);
		writer.string(entry.username);
		writer.string(entry.vehicle_content_id);
		writer.string(entry.vehicle_gameplay_digest);
		writer.string(entry.vehicle_package_digest);
		writer.string(entry.vehicle_workshop_id);
		writer.f32(entry.accel_setting);
		for (int32_t sticker : entry.stickers) writer.i32(sticker);
		if (!(entry.flags & FLAG_HAS_LIVERY)) continue;
		writer.string(entry.livery.vehicle_content_id);
		writer.u32(entry.livery.primary_colour.to_rgba32());
		writer.u32(entry.livery.secondary_colour.to_rgba32());
		writer.u32(entry.livery.accent_colour.to_rgba32());
		writer.u32(entry.livery.outline_colour.to_rgba32());
		writer.u32(entry.livery.trail_colour.to_rgba32());
		writer.u8((entry.livery.outline_colour_customized ? 1u : 0u) | (entry.livery.trail_colour_customized ? 2u : 0u));
		writer.u8(static_cast<uint8_t>(entry.livery.stamps.size()));
		for (const Stamp &stamp : entry.livery.stamps) {
			writer.u8(stamp.flags);
			writer.string(stamp.stamp_id);
			writer.string(stamp.custom_hash);
			writer.i32(stamp.palette_id);
			writer.i32(stamp.layer);
			write_vector2(writer, stamp.custom_rect.position);
			write_vector2(writer, stamp.custom_rect.size);
			write_vector3(writer, stamp.local_origin);
			write_vector3(writer, stamp.local_basis.get_column(0));
			write_vector3(writer, stamp.local_basis.get_column(1));
			write_vector3(writer, stamp.local_basis.get_column(2));
			writer.f32(stamp.rotation);
			write_vector2(writer, stamp.size);
			writer.f32(stamp.projection_depth);
			writer.u32(stamp.colour.to_rgba32());
			writer.f32(stamp.opacity);
		}
	}
	return writer.packed();
}

bool MxtRaceRoster::decode_wire(const PackedByteArray &bytes) {
	last_error = String();
	mxt_wire::Reader reader(bytes);
	uint8_t magic[sizeof(WIRE_MAGIC)];
	uint8_t version = 0;
	uint16_t entry_count = 0;
	if (!reader.raw(magic, sizeof(magic)) || std::memcmp(magic, WIRE_MAGIC, sizeof(magic)) != 0 ||
			!reader.u8(version) || version != WIRE_VERSION || !reader.u16(entry_count) || entry_count > MAX_RACERS) {
		last_error = "Malformed race roster packet.";
		return false;
	}
	std::vector<Entry> decoded;
	decoded.resize(entry_count);
	for (Entry &entry : decoded) {
		if (!reader.i64(entry.player_id) || entry.player_id <= 0 || !reader.i64(entry.network_peer_id) ||
				!reader.u8(entry.flags) || (entry.flags & 0xe0u) != 0 ||
				!reader.string(entry.username, MAX_USERNAME_BYTES) || !reader.string(entry.vehicle_content_id, MAX_FIELD_BYTES) ||
				!reader.string(entry.vehicle_gameplay_digest, MAX_FIELD_BYTES) || !reader.string(entry.vehicle_package_digest, MAX_FIELD_BYTES) ||
				!reader.string(entry.vehicle_workshop_id, MAX_FIELD_BYTES) || !reader.f32(entry.accel_setting) ||
				!std::isfinite(entry.accel_setting)) {
			last_error = "Malformed race roster entry.";
			return false;
		}
		entry.accel_setting = std::clamp(entry.accel_setting, 0.0f, 1.0f);
		for (int32_t &sticker : entry.stickers) if (!reader.i32(sticker)) {
			last_error = "Malformed race roster sticker data.";
			return false;
		}
		if (!(entry.flags & FLAG_HAS_LIVERY)) continue;
		uint32_t colours[5];
		uint8_t livery_flags = 0;
		uint8_t stamp_count = 0;
		if (!reader.string(entry.livery.vehicle_content_id, MAX_FIELD_BYTES) ||
				!reader.u32(colours[0]) || !reader.u32(colours[1]) || !reader.u32(colours[2]) || !reader.u32(colours[3]) || !reader.u32(colours[4]) ||
				!reader.u8(livery_flags) || (livery_flags & 0xfcu) != 0 || !reader.u8(stamp_count) || stamp_count > MAX_STAMPS) {
			last_error = "Malformed race roster livery.";
			return false;
		}
		entry.livery.primary_colour = Color::hex(colours[0]);
		entry.livery.secondary_colour = Color::hex(colours[1]);
		entry.livery.accent_colour = Color::hex(colours[2]);
		entry.livery.outline_colour = Color::hex(colours[3]);
		entry.livery.trail_colour = Color::hex(colours[4]);
		entry.livery.outline_colour_customized = (livery_flags & 1u) != 0;
		entry.livery.trail_colour_customized = (livery_flags & 2u) != 0;
		entry.livery.stamps.resize(stamp_count);
		for (Stamp &stamp : entry.livery.stamps) {
			Vector2 rect_position;
			Vector2 rect_size;
			Vector3 basis_x;
			Vector3 basis_y;
			Vector3 basis_z;
			uint32_t colour = 0;
			if (!reader.u8(stamp.flags) || (stamp.flags & 0xc0u) != 0 ||
					!reader.string(stamp.stamp_id, MAX_FIELD_BYTES) || !reader.string(stamp.custom_hash, MAX_FIELD_BYTES) ||
					!reader.i32(stamp.palette_id) || !reader.i32(stamp.layer) || !read_vector2(reader, rect_position) ||
					!read_vector2(reader, rect_size) || !read_vector3(reader, stamp.local_origin) ||
					!read_vector3(reader, basis_x) || !read_vector3(reader, basis_y) || !read_vector3(reader, basis_z) ||
					!reader.f32(stamp.rotation) || !read_vector2(reader, stamp.size) || !reader.f32(stamp.projection_depth) ||
					!reader.u32(colour) || !reader.f32(stamp.opacity) || !std::isfinite(stamp.rotation) ||
					!std::isfinite(stamp.projection_depth) || !std::isfinite(stamp.opacity)) {
				last_error = "Malformed race roster livery stamp.";
				return false;
			}
			stamp.custom_rect = Rect2(rect_position, Vector2(std::max(0.0f, rect_size.x), std::max(0.0f, rect_size.y)));
			stamp.local_basis = Basis(basis_x, basis_y, basis_z);
			if (std::abs(stamp.local_basis.determinant()) <= 1.0e-8f) stamp.local_basis = Basis();
			stamp.size.x = std::max(0.001f, stamp.size.x);
			stamp.size.y = std::max(0.001f, stamp.size.y);
			stamp.projection_depth = std::max(0.001f, stamp.projection_depth);
			stamp.colour = Color::hex(colour);
			stamp.opacity = std::clamp(stamp.opacity, 0.0f, 1.0f);
		}
	}
	if (!reader.finished()) {
		last_error = "Race roster packet has trailing data.";
		return false;
	}
	for (size_t i = 0; i < decoded.size(); ++i) for (size_t j = i + 1; j < decoded.size(); ++j) if (decoded[i].player_id == decoded[j].player_id) {
		last_error = "Race roster packet contains duplicate player IDs.";
		return false;
	}
	entries.swap(decoded);
	return true;
}
