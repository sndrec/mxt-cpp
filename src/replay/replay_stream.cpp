#include "replay/replay_stream.h"

#include "core/player_input.h"
#include "gamesim/gamesim.h"

#include "godot_cpp/classes/file_access.hpp"
#include "godot_cpp/classes/json.hpp"
#include "godot_cpp/core/class_db.hpp"

#include "zstd.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace godot {
namespace {

constexpr uint8_t FILE_MAGIC[8] = {'M', 'X', 'T', 'R', 'P', 'L', 'Y', 0};
constexpr uint16_t FILE_VERSION = 1;
constexpr uint16_t HEADER_SIZE = 128;
constexpr uint32_t BLOCK_INDEX_SIZE = 32;
constexpr uint64_t MIN_METADATA_CAPACITY = 16ull * 1024ull;
constexpr uint64_t MAX_METADATA_CAPACITY = 16ull * 1024ull * 1024ull;
constexpr uint64_t MAX_FILE_BYTES = 1024ull * 1024ull * 1024ull;
constexpr int MAX_INPUT_BYTES = 6;

struct ByteWriter {
	std::vector<uint8_t> bytes;
	void u8(uint8_t v) { bytes.push_back(v); }
	void u16(uint16_t v) { u8(static_cast<uint8_t>(v)); u8(static_cast<uint8_t>(v >> 8)); }
	void u32(uint32_t v) { for (int s = 0; s < 32; s += 8) u8(static_cast<uint8_t>(v >> s)); }
	void u64(uint64_t v) { u32(static_cast<uint32_t>(v)); u32(static_cast<uint32_t>(v >> 32)); }
	void data(const uint8_t *p, size_t n) { bytes.insert(bytes.end(), p, p + n); }
};

struct ByteReader {
	const uint8_t *bytes = nullptr;
	size_t size = 0;
	size_t offset = 0;
	bool ok = true;
	uint8_t u8() { if (offset + 1 > size) { ok = false; return 0; } return bytes[offset++]; }
	uint16_t u16() { uint16_t v = u8(); v |= static_cast<uint16_t>(u8()) << 8; return v; }
	uint32_t u32() { uint32_t v = 0; for (int s = 0; s < 32; s += 8) v |= static_cast<uint32_t>(u8()) << s; return v; }
	uint64_t u64() { const uint64_t lo = u32(); return lo | (static_cast<uint64_t>(u32()) << 32); }
};

uint32_t crc32_bytes(const uint8_t *data, size_t size)
{
	uint32_t crc = UINT32_C(0xffffffff);
	for (size_t i = 0; i < size; ++i) {
		crc ^= data[i];
		for (int bit = 0; bit < 8; ++bit) {
			const uint32_t mask = 0u - (crc & 1u);
			crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & mask);
		}
	}
	return ~crc;
}

void store_u32_le(uint8_t *destination, uint32_t value)
{
	for (int shift = 0; shift < 32; shift += 8) destination[shift / 8] = static_cast<uint8_t>(value >> shift);
}

void store_u64_le(uint8_t *destination, uint64_t value)
{
	store_u32_le(destination, static_cast<uint32_t>(value));
	store_u32_le(destination + 4, static_cast<uint32_t>(value >> 32));
}

PackedByteArray packed_bytes(const std::vector<uint8_t> &bytes)
{
	PackedByteArray out;
	out.resize(static_cast<int64_t>(bytes.size()));
	if (!bytes.empty()) std::memcpy(out.ptrw(), bytes.data(), bytes.size());
	return out;
}

bool range_inside(uint64_t offset, uint64_t size, uint64_t file_size)
{
	return offset <= file_size && size <= file_size - offset;
}

uint64_t metadata_capacity_for_size(uint64_t metadata_size)
{
	if (metadata_size == 0 || metadata_size > MAX_METADATA_CAPACITY) return 0;
	uint64_t capacity = MIN_METADATA_CAPACITY;
	while (capacity < metadata_size) capacity <<= 1;
	return capacity;
}

bool valid_encoded_input(const uint8_t *bytes, size_t size)
{
	if (!bytes || size == 0 || size > MAX_INPUT_BYTES) return false;
	return static_cast<size_t>(PlayerInput::encoded_raw_size_from_mask(bytes[0])) == size;
}

} // namespace

void MxtReplayStream::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("begin_recording", "racer_ids", "cpu_flags"), &MxtReplayStream::begin_recording);
	ClassDB::bind_method(D_METHOD("append_game_sim_frame", "game_sim", "tick"), &MxtReplayStream::append_game_sim_frame);
	ClassDB::bind_method(D_METHOD("append_frame_inputs", "tick", "inputs"), &MxtReplayStream::append_frame_inputs);
	ClassDB::bind_method(D_METHOD("frame_count"), &MxtReplayStream::frame_count);
	ClassDB::bind_method(D_METHOD("input_byte_count"), &MxtReplayStream::input_byte_count);
	ClassDB::bind_method(D_METHOD("cursor"), &MxtReplayStream::cursor);
	ClassDB::bind_method(D_METHOD("retain_head"), &MxtReplayStream::retain_head);
	ClassDB::bind_method(D_METHOD("release_head", "head_id"), &MxtReplayStream::release_head);
	ClassDB::bind_method(D_METHOD("has_head", "head_id"), &MxtReplayStream::has_head);
	ClassDB::bind_method(D_METHOD("restore_head", "head_id"), &MxtReplayStream::restore_head);
	ClassDB::bind_method(D_METHOD("truncate_to", "frame_count"), &MxtReplayStream::truncate_to);
	ClassDB::bind_method(D_METHOD("copy_prefix_from", "source", "frame_count"), &MxtReplayStream::copy_prefix_from);
	ClassDB::bind_method(D_METHOD("write_file", "path", "metadata"), &MxtReplayStream::write_file);
	ClassDB::bind_method(D_METHOD("rewrite_metadata", "path", "metadata"), &MxtReplayStream::rewrite_metadata);
	ClassDB::bind_method(D_METHOD("load_file", "path", "metadata_only"), &MxtReplayStream::load_file, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("get_metadata"), &MxtReplayStream::get_metadata);
	ClassDB::bind_method(D_METHOD("get_roster_ids"), &MxtReplayStream::get_roster_ids);
	ClassDB::bind_method(D_METHOD("get_cpu_flags"), &MxtReplayStream::get_cpu_flags);
	ClassDB::bind_method(D_METHOD("read_frame", "frame_index"), &MxtReplayStream::read_frame);
	ClassDB::bind_method(D_METHOD("read_frame_range", "begin", "end"), &MxtReplayStream::read_frame_range);
	ClassDB::bind_method(D_METHOD("get_player_input_stream", "slot"), &MxtReplayStream::get_player_input_stream);
	ClassDB::bind_method(D_METHOD("get_stats"), &MxtReplayStream::get_stats);
	ClassDB::bind_method(D_METHOD("get_last_error"), &MxtReplayStream::get_last_error);
	ClassDB::bind_method(D_METHOD("is_loaded"), &MxtReplayStream::is_loaded);
	ClassDB::bind_method(D_METHOD("is_recording"), &MxtReplayStream::is_recording);
	ClassDB::bind_static_method("MxtReplayStream", D_METHOD("path_has_binary_magic", "path"), &MxtReplayStream::path_has_binary_magic);
}

void MxtReplayStream::set_error(const String &message)
{
	last_error = message;
}

void MxtReplayStream::reset_recording()
{
	active_chunk.reset();
	retained_heads.clear();
	next_head_id = 1;
	recording_frame_count = 0;
	recording_input_bytes = 0;
}

void MxtReplayStream::reset_loaded()
{
	loaded_metadata.clear();
	loaded_path = String();
	loaded_blocks.clear();
	loaded_frame_count = 0;
	loaded_source_bytes = 0;
	loaded_frames_offset = 0;
	loaded_frames_size = 0;
	cached_block_index = -1;
	cached_block_bytes.clear();
	cached_frame_offsets.clear();
}

bool MxtReplayStream::validate_roster(const Array &ids, const Array &cpu_flags)
{
	if (ids.is_empty() || ids.size() > MAX_RACERS || cpu_flags.size() != ids.size()) return false;
	std::vector<int32_t> seen;
	seen.reserve(ids.size());
	for (int i = 0; i < ids.size(); ++i) {
		const int64_t raw = static_cast<int64_t>(ids[i]);
		if (raw < 0 || raw > std::numeric_limits<int32_t>::max()) return false;
		const int32_t id = static_cast<int32_t>(raw);
		if (std::find(seen.begin(), seen.end(), id) != seen.end()) return false;
		seen.push_back(id);
	}
	roster_ids = ids.duplicate(true);
	roster_cpu_flags = cpu_flags.duplicate(true);
	roster_id_values = std::move(seen);
	return true;
}

void MxtReplayStream::begin_recording(const Array &p_racer_ids, const Array &p_cpu_flags)
{
	last_error = String();
	reset_loaded();
	reset_recording();
	roster_ids.clear();
	roster_cpu_flags.clear();
	roster_id_values.clear();
	if (!validate_roster(p_racer_ids, p_cpu_flags)) set_error("invalid replay roster");
}

bool MxtReplayStream::append_encoded_frame(uint32_t tick, const uint8_t *bytes, size_t size, uint64_t input_bytes)
{
	if (roster_id_values.empty() || tick != recording_frame_count || recording_frame_count >= MAX_FRAMES || !bytes || size == 0) return false;
	if (!active_chunk || active_chunk->sealed || active_chunk->frame_offsets.size() - 1 >= TIMELINE_CHUNK_FRAMES) {
		auto next = std::make_shared<TimelineChunk>();
		next->parent = active_chunk;
		next->start_frame = recording_frame_count;
		next->start_input_bytes = recording_input_bytes;
		active_chunk = std::move(next);
	}
	active_chunk->bytes.insert(active_chunk->bytes.end(), bytes, bytes + size);
	active_chunk->frame_offsets.push_back(static_cast<uint32_t>(active_chunk->bytes.size()));
	++recording_frame_count;
	recording_input_bytes += input_bytes;
	return true;
}

bool MxtReplayStream::append_game_sim_frame(GameSim *p_sim, int64_t p_tick)
{
	if (!p_sim || p_tick < 0 || p_tick != recording_frame_count || !p_sim->input_buffer || !p_sim->car_player_ids || p_sim->num_cars <= 0) return false;
	if (p_tick >= p_sim->tick || p_sim->tick - p_tick > GameSim::INPUT_BUFFER_LEN) return false;
	ByteWriter frame;
	frame.bytes.reserve(roster_id_values.size() * 4);
	uint64_t input_bytes = 0;
	const PlayerInput *sim_frame = p_sim->input_buffer + (p_tick % GameSim::INPUT_BUFFER_LEN) * p_sim->num_cars;
	for (int32_t id : roster_id_values) {
		const int car_index = p_sim->find_car_index_for_player(id);
		if (car_index < 0 || car_index >= p_sim->num_cars) return false;
		uint8_t encoded[8] = {};
		const int count = PlayerInput::encode_to_raw(sim_frame[car_index], encoded, sizeof(encoded));
		if (count <= 0 || count > MAX_INPUT_BYTES) return false;
		frame.u8(static_cast<uint8_t>(count));
		frame.data(encoded, count);
		input_bytes += count;
	}
	return append_encoded_frame(static_cast<uint32_t>(p_tick), frame.bytes.data(), frame.bytes.size(), input_bytes);
}

bool MxtReplayStream::append_frame_inputs(int64_t p_tick, const Dictionary &p_inputs)
{
	if (p_tick < 0 || p_tick != recording_frame_count || p_inputs.is_empty()) return false;
	ByteWriter frame;
	uint64_t input_bytes = 0;
	for (int32_t id : roster_id_values) {
		Variant value = p_inputs.get(id, Variant());
		if (value.get_type() == Variant::NIL) value = p_inputs.get(String::num_int64(id), Variant());
		if (value.get_type() != Variant::PACKED_BYTE_ARRAY) return false;
		const PackedByteArray input = value;
		if (!valid_encoded_input(input.ptr(), input.size())) return false;
		frame.u8(static_cast<uint8_t>(input.size()));
		frame.data(input.ptr(), input.size());
		input_bytes += input.size();
	}
	return append_encoded_frame(static_cast<uint32_t>(p_tick), frame.bytes.data(), frame.bytes.size(), input_bytes);
}

int64_t MxtReplayStream::frame_count() const { return loaded_path.is_empty() ? recording_frame_count : static_cast<int64_t>(loaded_frame_count); }
int64_t MxtReplayStream::input_byte_count() const { return static_cast<int64_t>(recording_input_bytes); }
int64_t MxtReplayStream::cursor() const { return recording_frame_count; }

int64_t MxtReplayStream::retain_head()
{
	if (active_chunk) active_chunk->sealed = true;
	const int64_t id = next_head_id++;
	retained_heads[id] = {active_chunk, recording_frame_count, recording_input_bytes};
	return id;
}

void MxtReplayStream::release_head(int64_t p_head_id) { retained_heads.erase(p_head_id); }
bool MxtReplayStream::has_head(int64_t p_head_id) const { return retained_heads.find(p_head_id) != retained_heads.end(); }

bool MxtReplayStream::restore_head(int64_t p_head_id)
{
	const auto found = retained_heads.find(p_head_id);
	if (found == retained_heads.end()) return false;
	active_chunk = found->second.chunk;
	recording_frame_count = found->second.frame_count;
	recording_input_bytes = found->second.input_bytes;
	return true;
}

bool MxtReplayStream::truncate_to(int64_t p_frame_count)
{
	if (p_frame_count < 0 || p_frame_count > recording_frame_count) return false;
	const uint32_t target = static_cast<uint32_t>(p_frame_count);
	if (target == recording_frame_count) return true;
	if (target == 0) {
		active_chunk.reset(); recording_frame_count = 0; recording_input_bytes = 0; return true;
	}
	auto chunk = active_chunk;
	while (chunk) {
		const uint32_t own_count = static_cast<uint32_t>(chunk->frame_offsets.size() - 1);
		const uint32_t end = chunk->start_frame + own_count;
		if (target == end) {
			active_chunk = chunk; recording_frame_count = target;
			recording_input_bytes = chunk->start_input_bytes;
			for (uint32_t i = 0; i < own_count; ++i) {
				const uint8_t *frame = chunk->bytes.data() + chunk->frame_offsets[i];
				size_t offset = 0;
				for (size_t slot = 0; slot < roster_id_values.size(); ++slot) { const uint8_t n = frame[offset++]; offset += n; recording_input_bytes += n; }
			}
			return true;
		}
		if (target == chunk->start_frame) {
			active_chunk = chunk->parent; recording_frame_count = target; recording_input_bytes = chunk->start_input_bytes; return true;
		}
		if (target > chunk->start_frame && target < end) {
			const uint32_t keep = target - chunk->start_frame;
			auto partial = std::make_shared<TimelineChunk>();
			partial->parent = chunk->parent;
			partial->start_frame = chunk->start_frame;
			partial->start_input_bytes = chunk->start_input_bytes;
			partial->bytes.assign(chunk->bytes.begin(), chunk->bytes.begin() + chunk->frame_offsets[keep]);
			partial->frame_offsets.assign(chunk->frame_offsets.begin(), chunk->frame_offsets.begin() + keep + 1);
			active_chunk = partial;
			recording_frame_count = target;
			recording_input_bytes = partial->start_input_bytes;
			for (uint32_t i = 0; i < keep; ++i) {
				const uint8_t *frame = partial->bytes.data() + partial->frame_offsets[i];
				size_t offset = 0;
				for (size_t slot = 0; slot < roster_id_values.size(); ++slot) { const uint8_t n = frame[offset++]; offset += n; recording_input_bytes += n; }
			}
			return true;
		}
		chunk = chunk->parent;
	}
	return false;
}

bool MxtReplayStream::active_frame_bytes(uint32_t frame_index, const uint8_t *&out_bytes, size_t &out_size) const
{
	auto chunk = active_chunk;
	while (chunk) {
		const uint32_t own_count = static_cast<uint32_t>(chunk->frame_offsets.size() - 1);
		if (frame_index >= chunk->start_frame && frame_index < chunk->start_frame + own_count) {
			const uint32_t local = frame_index - chunk->start_frame;
			out_bytes = chunk->bytes.data() + chunk->frame_offsets[local];
			out_size = chunk->frame_offsets[local + 1] - chunk->frame_offsets[local];
			return true;
		}
		chunk = chunk->parent;
	}
	return false;
}

bool MxtReplayStream::copy_prefix_from(const Ref<MxtReplayStream> &p_source, int64_t p_frame_count)
{
	if (p_source.is_null() || p_frame_count < 0 || p_frame_count > p_source->frame_count()) return false;
	begin_recording(p_source->get_roster_ids(), p_source->get_cpu_flags());
	if (!last_error.is_empty()) return false;
	for (int64_t tick = 0; tick < p_frame_count; ++tick) {
		const uint8_t *frame = nullptr;
		size_t frame_size = 0;
		const bool found = p_source->loaded_path.is_empty()
				? p_source->active_frame_bytes(static_cast<uint32_t>(tick), frame, frame_size)
				: p_source->loaded_frame_bytes(static_cast<uint32_t>(tick), frame, frame_size);
		if (!found) return false;
		size_t offset = 0;
		uint64_t input_bytes = 0;
		for (size_t slot = 0; slot < roster_id_values.size(); ++slot) {
			if (offset >= frame_size) return false;
			const uint8_t count = frame[offset++];
			if (count == 0 || count > MAX_INPUT_BYTES || offset + count > frame_size) return false;
			offset += count;
			input_bytes += count;
		}
		if (offset != frame_size || !append_encoded_frame(static_cast<uint32_t>(tick), frame, frame_size, input_bytes)) return false;
	}
	return true;
}

bool MxtReplayStream::write_file(const String &p_path, const Dictionary &p_metadata)
{
	last_error = String();
	if (recording_frame_count == 0 || roster_id_values.empty()) { set_error("replay has no frames"); return false; }
	Dictionary metadata = p_metadata.duplicate(true);
	metadata["duration_ticks"] = static_cast<int64_t>(recording_frame_count);
	const CharString metadata_utf8 = JSON::stringify(metadata, String(), false, true).utf8();
	const size_t metadata_size = metadata_utf8.length();
	const uint64_t metadata_capacity = metadata_capacity_for_size(metadata_size);
	if (metadata_capacity == 0) { set_error("replay metadata is too large"); return false; }
	std::vector<uint8_t> metadata_bytes(reinterpret_cast<const uint8_t *>(metadata_utf8.get_data()), reinterpret_cast<const uint8_t *>(metadata_utf8.get_data()) + metadata_size);

	ByteWriter roster;
	for (size_t i = 0; i < roster_id_values.size(); ++i) {
		roster.u32(static_cast<uint32_t>(roster_id_values[i]));
		roster.u8(static_cast<bool>(roster_cpu_flags[static_cast<int>(i)]) ? 1 : 0);
		roster.u8(0); roster.u8(0); roster.u8(0);
	}

	struct CompressedBlock { BlockIndex index; std::vector<uint8_t> bytes; };
	std::vector<CompressedBlock> blocks;
	for (uint32_t start = 0; start < recording_frame_count; start += FILE_BLOCK_FRAMES) {
		const uint32_t count = std::min(FILE_BLOCK_FRAMES, recording_frame_count - start);
		std::vector<uint8_t> raw;
		for (uint32_t tick = start; tick < start + count; ++tick) {
			const uint8_t *frame = nullptr; size_t frame_size = 0;
			if (!active_frame_bytes(tick, frame, frame_size)) { set_error("recording timeline is discontinuous"); return false; }
			raw.insert(raw.end(), frame, frame + frame_size);
		}
		CompressedBlock block;
		block.index.start_tick = start;
		block.index.tick_count = count;
		block.index.uncompressed_size = static_cast<uint32_t>(raw.size());
		block.index.raw_crc32 = crc32_bytes(raw.data(), raw.size());
		block.bytes.resize(ZSTD_compressBound(raw.size()));
		const size_t compressed = ZSTD_compress(block.bytes.data(), block.bytes.size(), raw.data(), raw.size(), 1);
		if (ZSTD_isError(compressed) || compressed > std::numeric_limits<uint32_t>::max()) { set_error("replay frame compression failed"); return false; }
		block.bytes.resize(compressed);
		block.index.compressed_size = static_cast<uint32_t>(compressed);
		block.index.compressed_crc32 = crc32_bytes(block.bytes.data(), block.bytes.size());
		blocks.push_back(std::move(block));
	}

	const uint64_t metadata_offset = HEADER_SIZE;
	const uint64_t roster_offset = metadata_offset + metadata_capacity;
	const uint64_t index_offset = roster_offset + roster.bytes.size();
	const uint64_t index_size = blocks.size() * BLOCK_INDEX_SIZE;
	const uint64_t frames_offset = index_offset + index_size;
	uint64_t relative_frame_offset = 0;
	ByteWriter index;
	uint32_t frames_crc = UINT32_C(0xffffffff);
	for (CompressedBlock &block : blocks) {
		block.index.file_offset = relative_frame_offset;
		index.u32(block.index.start_tick); index.u32(block.index.tick_count); index.u64(block.index.file_offset);
		index.u32(block.index.compressed_size); index.u32(block.index.uncompressed_size);
		index.u32(block.index.raw_crc32); index.u32(block.index.compressed_crc32);
		relative_frame_offset += block.bytes.size();
		for (uint8_t byte : block.bytes) { frames_crc ^= byte; for (int bit = 0; bit < 8; ++bit) { const uint32_t mask = 0u - (frames_crc & 1u); frames_crc = (frames_crc >> 1) ^ (UINT32_C(0xedb88320) & mask); } }
	}
	frames_crc = ~frames_crc;
	const uint64_t file_size = frames_offset + relative_frame_offset;
	if (file_size > MAX_FILE_BYTES) { set_error("replay file is too large"); return false; }

	ByteWriter header;
	header.data(FILE_MAGIC, sizeof(FILE_MAGIC)); header.u16(FILE_VERSION); header.u16(HEADER_SIZE); header.u32(0);
	header.u64(metadata_offset); header.u64(metadata_bytes.size()); header.u64(roster_offset); header.u64(roster.bytes.size());
	header.u64(index_offset); header.u64(index.bytes.size()); header.u64(frames_offset); header.u64(relative_frame_offset);
	header.u64(recording_frame_count); header.u32(static_cast<uint32_t>(roster_id_values.size())); header.u32(static_cast<uint32_t>(blocks.size()));
	header.u32(crc32_bytes(metadata_bytes.data(), metadata_bytes.size())); header.u32(crc32_bytes(roster.bytes.data(), roster.bytes.size()));
	header.u32(crc32_bytes(index.bytes.data(), index.bytes.size())); header.u32(frames_crc); header.u64(recording_input_bytes);
	header.u32(0); header.u32(0);
	if (header.bytes.size() != HEADER_SIZE) { set_error("internal replay header size mismatch"); return false; }
	const uint32_t header_crc = crc32_bytes(header.bytes.data(), 120);
	header.bytes[120] = static_cast<uint8_t>(header_crc); header.bytes[121] = static_cast<uint8_t>(header_crc >> 8);
	header.bytes[122] = static_cast<uint8_t>(header_crc >> 16); header.bytes[123] = static_cast<uint8_t>(header_crc >> 24);

	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
	if (file.is_null()) { set_error("could not open replay output"); return false; }
	file->store_buffer(packed_bytes(header.bytes)); file->store_buffer(packed_bytes(metadata_bytes));
	if (metadata_bytes.size() < metadata_capacity) {
		std::vector<uint8_t> padding(metadata_capacity - metadata_bytes.size(), 0);
		file->store_buffer(packed_bytes(padding));
	}
	file->store_buffer(packed_bytes(roster.bytes)); file->store_buffer(packed_bytes(index.bytes));
	for (const CompressedBlock &block : blocks) file->store_buffer(packed_bytes(block.bytes));
	file->flush();
	if (file->get_error() != OK) { set_error("could not write complete replay file"); return false; }
	return true;
}

bool MxtReplayStream::rewrite_metadata(const String &p_path, const Dictionary &p_metadata)
{
	last_error = String();
	Ref<MxtReplayStream> source;
	source.instantiate();
	if (!source->load_file(p_path, true)) { set_error(source->get_last_error()); return false; }
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ_WRITE);
	if (file.is_null()) { set_error("could not open replay metadata for update"); return false; }
	PackedByteArray header_data = file->get_buffer(HEADER_SIZE);
	if (header_data.size() != HEADER_SIZE || std::memcmp(header_data.ptr(), FILE_MAGIC, 8) != 0) { set_error("invalid replay header"); return false; }
	ByteReader header_reader{header_data.ptr(), static_cast<size_t>(header_data.size()), 16};
	const uint64_t metadata_offset = header_reader.u64();
	header_reader.u64();
	const uint64_t roster_offset = header_reader.u64();
	if (!header_reader.ok || metadata_offset != HEADER_SIZE || roster_offset <= metadata_offset) { set_error("invalid replay metadata capacity"); return false; }
	const uint64_t metadata_capacity = roster_offset - metadata_offset;
	Dictionary metadata = p_metadata.duplicate(true);
	metadata["duration_ticks"] = source->frame_count();
	const CharString metadata_utf8 = JSON::stringify(metadata, String(), false, true).utf8();
	const size_t metadata_size = metadata_utf8.length();
	if (metadata_size == 0 || metadata_size > metadata_capacity) { set_error("replay metadata is too large"); return false; }
	std::vector<uint8_t> metadata_section(metadata_capacity, 0);
	std::memcpy(metadata_section.data(), metadata_utf8.get_data(), metadata_size);
	uint8_t *header = header_data.ptrw();
	store_u64_le(header + 24, metadata_size);
	store_u32_le(header + 96, crc32_bytes(metadata_section.data(), metadata_size));
	store_u32_le(header + 120, 0);
	store_u32_le(header + 120, crc32_bytes(header, 120));
	file->seek(0);
	file->store_buffer(header_data);
	file->store_buffer(packed_bytes(metadata_section));
	file->flush();
	if (file->get_error() != OK) { set_error("could not update replay metadata"); return false; }
	return true;
}

bool MxtReplayStream::path_has_binary_magic(const String &p_path)
{
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ);
	if (file.is_null() || file->get_length() < 8) return false;
	const PackedByteArray bytes = file->get_buffer(8);
	return bytes.size() == 8 && std::memcmp(bytes.ptr(), FILE_MAGIC, 8) == 0;
}

bool MxtReplayStream::load_file(const String &p_path, bool p_metadata_only)
{
	last_error = String(); reset_recording(); reset_loaded(); roster_ids.clear(); roster_cpu_flags.clear(); roster_id_values.clear();
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ);
	if (file.is_null()) { set_error("could not open replay file"); return false; }
	const uint64_t file_size = file->get_length();
	if (file_size < HEADER_SIZE || file_size > MAX_FILE_BYTES) { set_error("invalid replay file size"); return false; }
	const PackedByteArray header_data = file->get_buffer(HEADER_SIZE);
	if (header_data.size() != HEADER_SIZE || std::memcmp(header_data.ptr(), FILE_MAGIC, 8) != 0) { set_error("invalid replay magic"); return false; }
	std::vector<uint8_t> header_copy(header_data.ptr(), header_data.ptr() + HEADER_SIZE);
	ByteReader header{header_data.ptr(), static_cast<size_t>(header_data.size()), 8};
	const uint16_t version = header.u16(); const uint16_t header_size = header.u16(); header.u32();
	const uint64_t metadata_offset = header.u64(); const uint64_t metadata_size = header.u64();
	const uint64_t roster_offset = header.u64(); const uint64_t roster_size = header.u64();
	const uint64_t index_offset = header.u64(); const uint64_t index_size = header.u64();
	const uint64_t frames_offset = header.u64(); const uint64_t frames_size = header.u64();
	const uint64_t duration = header.u64(); const uint32_t roster_count = header.u32(); const uint32_t block_count = header.u32();
	const uint32_t metadata_crc = header.u32(); const uint32_t roster_crc = header.u32(); const uint32_t index_crc = header.u32(); const uint32_t frames_crc = header.u32();
	const uint64_t input_bytes = header.u64(); const uint32_t header_crc = header.u32(); header.u32();
	const uint64_t metadata_capacity = roster_offset > metadata_offset ? roster_offset - metadata_offset : 0;
	std::fill(header_copy.begin() + 120, header_copy.begin() + 124, 0);
	if (!header.ok || version != FILE_VERSION || header_size != HEADER_SIZE || crc32_bytes(header_copy.data(), 120) != header_crc \
			|| metadata_capacity < MIN_METADATA_CAPACITY || metadata_capacity > MAX_METADATA_CAPACITY \
			|| (metadata_capacity & (metadata_capacity - 1)) != 0 || metadata_size == 0 || metadata_size > metadata_capacity \
			|| roster_count == 0 || roster_count > MAX_RACERS \
			|| duration == 0 || duration > MAX_FRAMES || block_count == 0 || index_size != static_cast<uint64_t>(block_count) * BLOCK_INDEX_SIZE \
			|| roster_size != static_cast<uint64_t>(roster_count) * 8 || metadata_offset != HEADER_SIZE \
			|| index_offset != roster_offset + roster_size \
			|| frames_offset != index_offset + index_size || !range_inside(frames_offset, frames_size, file_size) || frames_offset + frames_size != file_size) {
		set_error("invalid replay header or section bounds"); return false;
	}
	file->seek(metadata_offset); const PackedByteArray metadata_data = file->get_buffer(metadata_size);
	if (metadata_data.size() != metadata_size || crc32_bytes(metadata_data.ptr(), metadata_data.size()) != metadata_crc) { set_error("replay metadata checksum mismatch"); return false; }
	const Variant parsed = JSON::parse_string(String::utf8(reinterpret_cast<const char *>(metadata_data.ptr()), metadata_data.size()));
	if (parsed.get_type() != Variant::DICTIONARY) { set_error("invalid replay metadata"); return false; }
	loaded_metadata = parsed;
	loaded_metadata["duration_ticks"] = static_cast<int64_t>(duration);
	loaded_metadata["_native_input_bytes"] = static_cast<int64_t>(input_bytes);
	loaded_metadata["_native_replay"] = true;
	loaded_path = p_path; loaded_frame_count = duration; loaded_source_bytes = file_size; loaded_frames_offset = frames_offset; loaded_frames_size = frames_size;
	if (p_metadata_only) return true;

	file->seek(roster_offset); const PackedByteArray roster_data = file->get_buffer(roster_size);
	if (roster_data.size() != roster_size || crc32_bytes(roster_data.ptr(), roster_data.size()) != roster_crc) { set_error("replay roster checksum mismatch"); reset_loaded(); return false; }
	ByteReader roster_reader{roster_data.ptr(), static_cast<size_t>(roster_data.size()), 0}; Array ids; Array cpus;
	for (uint32_t i = 0; i < roster_count; ++i) { ids.append(static_cast<int64_t>(static_cast<int32_t>(roster_reader.u32()))); cpus.append(roster_reader.u8() != 0); roster_reader.u8(); roster_reader.u8(); roster_reader.u8(); }
	if (!roster_reader.ok || !validate_roster(ids, cpus)) { set_error("invalid replay roster"); reset_loaded(); return false; }

	file->seek(index_offset); const PackedByteArray index_data = file->get_buffer(index_size);
	if (index_data.size() != index_size || crc32_bytes(index_data.ptr(), index_data.size()) != index_crc) { set_error("replay block index checksum mismatch"); reset_loaded(); return false; }
	ByteReader index_reader{index_data.ptr(), static_cast<size_t>(index_data.size()), 0}; uint64_t expected_offset = 0; uint64_t expected_tick = 0;
	loaded_blocks.resize(block_count);
	for (uint32_t i = 0; i < block_count; ++i) {
		BlockIndex &block = loaded_blocks[i]; block.start_tick = index_reader.u32(); block.tick_count = index_reader.u32(); block.file_offset = index_reader.u64();
		block.compressed_size = index_reader.u32(); block.uncompressed_size = index_reader.u32(); block.raw_crc32 = index_reader.u32(); block.compressed_crc32 = index_reader.u32();
		if (block.start_tick != expected_tick || block.tick_count == 0 || block.tick_count > FILE_BLOCK_FRAMES || block.file_offset != expected_offset \
				|| block.compressed_size == 0 || block.uncompressed_size == 0 || !range_inside(block.file_offset, block.compressed_size, frames_size)) {
			set_error("invalid replay block index"); reset_loaded(); return false;
		}
		expected_tick += block.tick_count; expected_offset += block.compressed_size;
	}
	if (!index_reader.ok || expected_tick != duration || expected_offset != frames_size) { set_error("replay block index is discontinuous"); reset_loaded(); return false; }
	file->seek(frames_offset); uint32_t computed_frames_crc = UINT32_C(0xffffffff); uint64_t remaining = frames_size;
	while (remaining > 0) { const int64_t count = static_cast<int64_t>(std::min<uint64_t>(remaining, 1024 * 1024)); const PackedByteArray part = file->get_buffer(count); if (part.size() != count) { set_error("truncated replay frames"); reset_loaded(); return false; } for (uint8_t byte : part) { computed_frames_crc ^= byte; for (int bit = 0; bit < 8; ++bit) { const uint32_t mask = 0u - (computed_frames_crc & 1u); computed_frames_crc = (computed_frames_crc >> 1) ^ (UINT32_C(0xedb88320) & mask); } } remaining -= count; }
	if (~computed_frames_crc != frames_crc) { set_error("replay frame section checksum mismatch"); reset_loaded(); return false; }
	return true;
}

bool MxtReplayStream::parse_frame_offsets(const std::vector<uint8_t> &bytes, uint32_t tick_count, std::vector<uint32_t> &out_offsets) const
{
	out_offsets.clear(); out_offsets.reserve(tick_count + 1); size_t offset = 0; out_offsets.push_back(0);
	for (uint32_t tick = 0; tick < tick_count; ++tick) {
		for (size_t slot = 0; slot < roster_id_values.size(); ++slot) {
			if (offset >= bytes.size()) return false; const uint8_t count = bytes[offset++];
			if (count == 0 || count > MAX_INPUT_BYTES || offset + count > bytes.size() || !valid_encoded_input(bytes.data() + offset, count)) return false;
			offset += count;
		}
		out_offsets.push_back(static_cast<uint32_t>(offset));
	}
	return offset == bytes.size();
}

bool MxtReplayStream::ensure_loaded_block(int block_index)
{
	if (block_index == cached_block_index) return true;
	if (block_index < 0 || block_index >= static_cast<int>(loaded_blocks.size())) return false;
	const BlockIndex &block = loaded_blocks[block_index]; Ref<FileAccess> file = FileAccess::open(loaded_path, FileAccess::READ);
	if (file.is_null()) { set_error("could not reopen replay file"); return false; }
	file->seek(loaded_frames_offset + block.file_offset); const PackedByteArray compressed = file->get_buffer(block.compressed_size);
	if (compressed.size() != block.compressed_size || crc32_bytes(compressed.ptr(), compressed.size()) != block.compressed_crc32) { set_error("replay block checksum mismatch"); return false; }
	std::vector<uint8_t> raw(block.uncompressed_size); const size_t result = ZSTD_decompress(raw.data(), raw.size(), compressed.ptr(), compressed.size());
	if (ZSTD_isError(result) || result != raw.size() || crc32_bytes(raw.data(), raw.size()) != block.raw_crc32) { set_error("replay block decompression failed"); return false; }
	std::vector<uint32_t> offsets; if (!parse_frame_offsets(raw, block.tick_count, offsets)) { set_error("replay block contains malformed inputs"); return false; }
	cached_block_bytes = std::move(raw); cached_frame_offsets = std::move(offsets); cached_block_index = block_index; return true;
}

bool MxtReplayStream::loaded_frame_bytes(uint32_t frame_index, const uint8_t *&out_bytes, size_t &out_size)
{
	if (frame_index >= loaded_frame_count) return false;
	const int block_index = static_cast<int>(frame_index / FILE_BLOCK_FRAMES);
	if (!ensure_loaded_block(block_index)) return false;
	const BlockIndex &block = loaded_blocks[block_index]; const uint32_t local = frame_index - block.start_tick;
	out_bytes = cached_block_bytes.data() + cached_frame_offsets[local]; out_size = cached_frame_offsets[local + 1] - cached_frame_offsets[local]; return true;
}

Dictionary MxtReplayStream::decode_frame(uint32_t tick, const uint8_t *bytes, size_t size) const
{
	Dictionary inputs; size_t offset = 0;
	for (size_t slot = 0; slot < roster_id_values.size(); ++slot) {
		if (offset >= size) return Dictionary(); const uint8_t count = bytes[offset++];
		if (offset + count > size || !valid_encoded_input(bytes + offset, count)) return Dictionary();
		PackedByteArray input; input.resize(count); std::memcpy(input.ptrw(), bytes + offset, count); offset += count; inputs[roster_id_values[slot]] = input;
	}
	if (offset != size) return Dictionary(); Dictionary out; out["tick"] = static_cast<int64_t>(tick); out["inputs"] = inputs; return out;
}

Dictionary MxtReplayStream::read_frame(int64_t p_frame_index)
{
	if (p_frame_index < 0 || p_frame_index >= frame_count()) return Dictionary();
	const uint8_t *bytes = nullptr; size_t size = 0;
	const bool ok = loaded_path.is_empty() ? active_frame_bytes(static_cast<uint32_t>(p_frame_index), bytes, size) : loaded_frame_bytes(static_cast<uint32_t>(p_frame_index), bytes, size);
	return ok ? decode_frame(static_cast<uint32_t>(p_frame_index), bytes, size) : Dictionary();
}

Array MxtReplayStream::read_frame_range(int64_t p_begin, int64_t p_end)
{
	const int64_t begin = std::max<int64_t>(0, p_begin); const int64_t end = std::min<int64_t>(frame_count(), std::max(begin, p_end)); Array out; out.resize(end - begin);
	for (int64_t i = begin; i < end; ++i) { const Dictionary frame = read_frame(i); if (frame.is_empty()) return Array(); out[i - begin] = frame; }
	return out;
}

Dictionary MxtReplayStream::get_player_input_stream(int64_t p_slot)
{
	if (p_slot < 0 || p_slot >= static_cast<int64_t>(roster_id_values.size())) return Dictionary();
	PackedByteArray bytes; bytes.resize(frame_count() * MAX_INPUT_BYTES); PackedInt32Array offsets; offsets.resize(frame_count() + 1); int64_t write = 0;
	for (int64_t tick = 0; tick < frame_count(); ++tick) {
		const uint8_t *frame = nullptr; size_t frame_size = 0; const bool ok = loaded_path.is_empty() ? active_frame_bytes(tick, frame, frame_size) : loaded_frame_bytes(tick, frame, frame_size); if (!ok) return Dictionary();
		size_t offset = 0; for (int64_t slot = 0; slot < p_slot; ++slot) { if (offset >= frame_size) return Dictionary(); offset += 1 + frame[offset]; }
		if (offset >= frame_size) return Dictionary(); const uint8_t count = frame[offset++]; if (offset + count > frame_size) return Dictionary();
		offsets[tick] = write; std::memcpy(bytes.ptrw() + write, frame + offset, count); write += count;
	}
	bytes.resize(write); offsets[frame_count()] = write; Dictionary out; out["input_bytes"] = bytes; out["frame_offsets"] = offsets; out["frame_count"] = frame_count(); return out;
}

Dictionary MxtReplayStream::get_metadata() const { return loaded_metadata.duplicate(true); }
Array MxtReplayStream::get_roster_ids() const { return roster_ids.duplicate(true); }
Array MxtReplayStream::get_cpu_flags() const { return roster_cpu_flags.duplicate(true); }
String MxtReplayStream::get_last_error() const { return last_error; }
bool MxtReplayStream::is_loaded() const { return !loaded_path.is_empty(); }
bool MxtReplayStream::is_recording() const { return loaded_path.is_empty() && !roster_id_values.empty(); }

Dictionary MxtReplayStream::get_stats() const
{
	Dictionary out; out["frame_count"] = frame_count(); out["racer_count"] = static_cast<int64_t>(roster_id_values.size());
	out["input_bytes"] = is_loaded() ? static_cast<int64_t>(loaded_metadata.get("_native_input_bytes", 0)) : static_cast<int64_t>(recording_input_bytes);
	out["source_bytes"] = static_cast<int64_t>(loaded_source_bytes); out["block_count"] = static_cast<int64_t>(loaded_blocks.size());
	out["cached_block_bytes"] = static_cast<int64_t>(cached_block_bytes.size()); out["retained_heads"] = static_cast<int64_t>(retained_heads.size()); return out;
}

} // namespace godot
