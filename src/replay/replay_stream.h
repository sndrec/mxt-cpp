#ifndef MXT_REPLAY_STREAM_H
#define MXT_REPLAY_STREAM_H

#include "godot_cpp/classes/ref_counted.hpp"
#include "godot_cpp/variant/array.hpp"
#include "godot_cpp/variant/dictionary.hpp"
#include "godot_cpp/variant/packed_byte_array.hpp"
#include "godot_cpp/variant/packed_int32_array.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace godot {

class GameSim;

class MxtReplayStream : public RefCounted {
	GDCLASS(MxtReplayStream, RefCounted)

	struct TimelineChunk {
		std::shared_ptr<TimelineChunk> parent;
		uint32_t start_frame = 0;
		uint64_t start_input_bytes = 0;
		std::vector<uint8_t> bytes;
		std::vector<uint32_t> frame_offsets{0};
		bool sealed = false;
	};

	struct RetainedHead {
		std::shared_ptr<TimelineChunk> chunk;
		uint32_t frame_count = 0;
		uint64_t input_bytes = 0;
	};

	struct BlockIndex {
		uint32_t start_tick = 0;
		uint32_t tick_count = 0;
		uint64_t file_offset = 0;
		uint32_t compressed_size = 0;
		uint32_t uncompressed_size = 0;
		uint32_t raw_crc32 = 0;
		uint32_t compressed_crc32 = 0;
	};

	static constexpr uint32_t TIMELINE_CHUNK_FRAMES = 256;
	static constexpr uint32_t FILE_BLOCK_FRAMES = 256;
	static constexpr uint32_t MAX_RACERS = 256;
	static constexpr uint64_t MAX_FRAMES = 60ull * 60ull * 60ull;

	Array roster_ids;
	Array roster_cpu_flags;
	std::vector<int32_t> roster_id_values;
	std::shared_ptr<TimelineChunk> active_chunk;
	std::unordered_map<int64_t, RetainedHead> retained_heads;
	int64_t next_head_id = 1;
	uint32_t recording_frame_count = 0;
	uint64_t recording_input_bytes = 0;

	Dictionary loaded_metadata;
	String loaded_path;
	std::vector<BlockIndex> loaded_blocks;
	uint64_t loaded_frame_count = 0;
	uint64_t loaded_source_bytes = 0;
	uint64_t loaded_frames_offset = 0;
	uint64_t loaded_frames_size = 0;
	int cached_block_index = -1;
	std::vector<uint8_t> cached_block_bytes;
	std::vector<uint32_t> cached_frame_offsets;
	String last_error;

	void reset_recording();
	void reset_loaded();
	bool append_encoded_frame(uint32_t tick, const uint8_t *bytes, size_t size, uint64_t input_bytes);
	bool active_frame_bytes(uint32_t frame_index, const uint8_t *&out_bytes, size_t &out_size) const;
	bool loaded_frame_bytes(uint32_t frame_index, const uint8_t *&out_bytes, size_t &out_size);
	bool ensure_loaded_block(int block_index);
	bool parse_frame_offsets(const std::vector<uint8_t> &bytes, uint32_t tick_count, std::vector<uint32_t> &out_offsets) const;
	Dictionary decode_frame(uint32_t tick, const uint8_t *bytes, size_t size) const;
	bool validate_roster(const Array &ids, const Array &cpu_flags);
	void set_error(const String &message);

protected:
	static void _bind_methods();

public:
	void begin_recording(const Array &p_racer_ids, const Array &p_cpu_flags);
	bool append_game_sim_frame(GameSim *p_sim, int64_t p_tick);
	bool append_frame_inputs(int64_t p_tick, const Dictionary &p_inputs);
	int64_t frame_count() const;
	int64_t input_byte_count() const;
	int64_t cursor() const;
	int64_t retain_head();
	void release_head(int64_t p_head_id);
	bool has_head(int64_t p_head_id) const;
	bool restore_head(int64_t p_head_id);
	bool truncate_to(int64_t p_frame_count);
	bool copy_prefix_from(const Ref<MxtReplayStream> &p_source, int64_t p_frame_count);
	bool write_file(const String &p_path, const Dictionary &p_metadata);
	bool rewrite_metadata(const String &p_path, const Dictionary &p_metadata);
	bool load_file(const String &p_path, bool p_metadata_only = false);
	Dictionary get_metadata() const;
	Array get_roster_ids() const;
	Array get_cpu_flags() const;
	Dictionary read_frame(int64_t p_frame_index);
	Array read_frame_range(int64_t p_begin, int64_t p_end);
	Dictionary get_player_input_stream(int64_t p_slot);
	Dictionary get_stats() const;
	String get_last_error() const;
	bool is_loaded() const;
	bool is_recording() const;
	static bool path_has_binary_magic(const String &p_path);
};

} // namespace godot

#endif
