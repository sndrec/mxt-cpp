#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstdint>
#include <vector>

namespace godot {

class MxtRaceRoster : public RefCounted {
	GDCLASS(MxtRaceRoster, RefCounted)

private:
	struct Stamp {
		String stamp_id;
		String custom_hash;
		int32_t palette_id = 0;
		int32_t layer = 0;
		Rect2 custom_rect;
		Vector3 local_origin;
		Basis local_basis;
		float rotation = 0.0f;
		Vector2 size = Vector2(1.0f, 1.0f);
		float projection_depth = 0.25f;
		Color colour = Color(1.0f, 1.0f, 1.0f, 1.0f);
		float opacity = 1.0f;
		uint8_t flags = 0;
	};

	struct Livery {
		String vehicle_content_id;
		Color primary_colour = Color(0.1f, 0.35f, 1.0f, 1.0f);
		Color secondary_colour = Color(1.0f, 1.0f, 1.0f, 1.0f);
		Color accent_colour = Color(0.05f, 0.05f, 0.06f, 1.0f);
		Color outline_colour = Color(0.25f, 0.55f, 1.0f, 1.0f);
		Color trail_colour = Color(0.25f, 0.55f, 1.0f, 1.0f);
		bool outline_colour_customized = false;
		bool trail_colour_customized = false;
		std::vector<Stamp> stamps;
	};

	struct Entry {
		int64_t player_id = 0;
		int64_t network_peer_id = 0;
		String username = "Player";
		String vehicle_content_id;
		String vehicle_gameplay_digest;
		String vehicle_package_digest;
		String vehicle_workshop_id;
		float accel_setting = 1.0f;
		int32_t stickers[4] = {0, 1, 2, 3};
		uint8_t flags = 0;
		Livery livery;
	};

	std::vector<Entry> entries;
	String last_error;

	bool parse_entry(int64_t player_id, int64_t network_peer_id, uint8_t flags, const Dictionary &settings, Entry &out);
	Dictionary entry_dictionary(const Entry &entry) const;

protected:
	static void _bind_methods();

public:
	enum EntryFlag : uint8_t {
		FLAG_CPU = 1u << 0,
		FLAG_BUMPER = 1u << 1,
		FLAG_SPECTATOR = 1u << 2,
		FLAG_DISCONNECTED = 1u << 3,
		FLAG_HAS_LIVERY = 1u << 4,
	};

	Ref<MxtRaceRoster> copy() const;
	void clear();
	int32_t count() const { return static_cast<int32_t>(entries.size()); }
	bool append_settings(int64_t player_id, int64_t network_peer_id, bool cpu, bool bumper, bool disconnected, const Dictionary &settings);
	bool upsert_settings(int64_t player_id, int64_t network_peer_id, bool cpu, bool bumper, bool disconnected, const Dictionary &settings);
	bool remove_player(int64_t player_id);
	int32_t find_player(int64_t player_id) const;
	int64_t get_player_id(int32_t index) const;
	int64_t get_network_peer_id(int32_t index) const;
	bool is_cpu(int32_t index) const;
	bool is_bumper(int32_t index) const;
	bool is_spectator(int32_t index) const;
	bool is_disconnected(int32_t index) const;
	Dictionary get_settings_dictionary(int32_t index) const;
	Dictionary get_player_settings_dictionary(int64_t player_id) const;
	String get_last_error() const { return last_error; }

	PackedByteArray encode_wire() const;
	bool decode_wire(const PackedByteArray &bytes);
};

} // namespace godot

VARIANT_ENUM_CAST(godot::MxtRaceRoster::EntryFlag)
