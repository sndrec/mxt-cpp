#include "core/track_content_evidence.h"

#include "core/bounded_wire.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>

#include <cstdint>
#include <cstring>

using namespace godot;

namespace {

static constexpr uint8_t WIRE_MAGIC[8] = {'M', 'X', 'T', 'T', 'R', 'K', 'E', 0};
static constexpr uint8_t WIRE_VERSION = 1;
static constexpr uint32_t MAX_TRACKS = 256;
static constexpr uint32_t MAX_FIELD_BYTES = 1024;

static bool dictionary_array(const Dictionary &value, const char *key, Array &out) {
	const Variant field = value.get(key, Array());
	if (field.get_type() != Variant::ARRAY) return false;
	out = field;
	return true;
}

} // namespace

void MxtTrackContentEvidence::_bind_methods() {
	ClassDB::bind_method(D_METHOD("copy"), &MxtTrackContentEvidence::copy);
	ClassDB::bind_method(D_METHOD("clear"), &MxtTrackContentEvidence::clear);
	ClassDB::bind_method(D_METHOD("count"), &MxtTrackContentEvidence::count);
	ClassDB::bind_method(D_METHOD("append", "content_id", "gameplay_digest", "package_digest", "workshop_id"), &MxtTrackContentEvidence::append);
	ClassDB::bind_method(D_METHOD("get_content_id", "index"), &MxtTrackContentEvidence::get_content_id);
	ClassDB::bind_method(D_METHOD("get_gameplay_digest", "index"), &MxtTrackContentEvidence::get_gameplay_digest);
	ClassDB::bind_method(D_METHOD("get_package_digest", "index"), &MxtTrackContentEvidence::get_package_digest);
	ClassDB::bind_method(D_METHOD("get_workshop_id", "index"), &MxtTrackContentEvidence::get_workshop_id);
	ClassDB::bind_method(D_METHOD("find_content_id", "content_id"), &MxtTrackContentEvidence::find_content_id);
	ClassDB::bind_method(D_METHOD("get_last_error"), &MxtTrackContentEvidence::get_last_error);
	ClassDB::bind_method(D_METHOD("encode_wire"), &MxtTrackContentEvidence::encode_wire);
	ClassDB::bind_method(D_METHOD("decode_wire", "bytes"), &MxtTrackContentEvidence::decode_wire);
	ClassDB::bind_method(D_METHOD("to_metadata_dictionary"), &MxtTrackContentEvidence::to_metadata_dictionary);
	ClassDB::bind_method(D_METHOD("load_metadata_dictionary", "value"), &MxtTrackContentEvidence::load_metadata_dictionary);
}

Ref<MxtTrackContentEvidence> MxtTrackContentEvidence::copy() const {
	Ref<MxtTrackContentEvidence> out;
	out.instantiate();
	out->entries = entries;
	return out;
}

void MxtTrackContentEvidence::clear() {
	entries.clear();
	last_error = String();
}

bool MxtTrackContentEvidence::append(const String &content_id, const String &gameplay_digest, const String &package_digest, const String &workshop_id) {
	if (entries.size() >= MAX_TRACKS || content_id.utf8().length() > MAX_FIELD_BYTES ||
			gameplay_digest.utf8().length() > MAX_FIELD_BYTES || package_digest.utf8().length() > MAX_FIELD_BYTES ||
			workshop_id.utf8().length() > MAX_FIELD_BYTES) {
		last_error = "Track content evidence exceeds its bounded wire limits.";
		return false;
	}
	entries.push_back({content_id, gameplay_digest, package_digest, workshop_id});
	return true;
}

String MxtTrackContentEvidence::get_content_id(int32_t index) const {
	return index >= 0 && static_cast<size_t>(index) < entries.size() ? entries[index].content_id : String();
}

String MxtTrackContentEvidence::get_gameplay_digest(int32_t index) const {
	return index >= 0 && static_cast<size_t>(index) < entries.size() ? entries[index].gameplay_digest : String();
}

String MxtTrackContentEvidence::get_package_digest(int32_t index) const {
	return index >= 0 && static_cast<size_t>(index) < entries.size() ? entries[index].package_digest : String();
}

String MxtTrackContentEvidence::get_workshop_id(int32_t index) const {
	return index >= 0 && static_cast<size_t>(index) < entries.size() ? entries[index].workshop_id : String();
}

int32_t MxtTrackContentEvidence::find_content_id(const String &content_id) const {
	for (size_t i = 0; i < entries.size(); ++i) {
		if (entries[i].content_id == content_id) return static_cast<int32_t>(i);
	}
	return -1;
}

PackedByteArray MxtTrackContentEvidence::encode_wire() const {
	mxt_wire::Writer writer;
	writer.raw(WIRE_MAGIC, sizeof(WIRE_MAGIC));
	writer.u8(WIRE_VERSION);
	writer.u16(static_cast<uint16_t>(entries.size()));
	for (const Entry &entry : entries) {
		writer.string(entry.content_id);
		writer.string(entry.gameplay_digest);
		writer.string(entry.package_digest);
		writer.string(entry.workshop_id);
	}
	return writer.packed();
}

bool MxtTrackContentEvidence::decode_wire(const PackedByteArray &bytes) {
	last_error = String();
	mxt_wire::Reader reader(bytes);
	uint8_t magic[sizeof(WIRE_MAGIC)];
	uint8_t version = 0;
	uint16_t entry_count = 0;
	if (!reader.raw(magic, sizeof(magic)) || std::memcmp(magic, WIRE_MAGIC, sizeof(magic)) != 0 ||
			!reader.u8(version) || version != WIRE_VERSION || !reader.u16(entry_count) || entry_count > MAX_TRACKS) {
		last_error = "Malformed track content evidence packet.";
		return false;
	}
	std::vector<Entry> decoded;
	decoded.resize(entry_count);
	for (Entry &entry : decoded) {
		if (!reader.string(entry.content_id, MAX_FIELD_BYTES) ||
				!reader.string(entry.gameplay_digest, MAX_FIELD_BYTES) ||
				!reader.string(entry.package_digest, MAX_FIELD_BYTES) ||
				!reader.string(entry.workshop_id, MAX_FIELD_BYTES)) {
			last_error = "Malformed track content evidence entry.";
			return false;
		}
	}
	if (!reader.finished()) {
		last_error = "Track content evidence packet has trailing data.";
		return false;
	}
	entries.swap(decoded);
	return true;
}

Dictionary MxtTrackContentEvidence::to_metadata_dictionary() const {
	Array content_ids;
	Array gameplay_digests;
	Array package_digests;
	Array workshop_ids;
	content_ids.resize(entries.size());
	gameplay_digests.resize(entries.size());
	package_digests.resize(entries.size());
	workshop_ids.resize(entries.size());
	for (size_t i = 0; i < entries.size(); ++i) {
		content_ids[i] = entries[i].content_id;
		gameplay_digests[i] = entries[i].gameplay_digest;
		package_digests[i] = entries[i].package_digest;
		workshop_ids[i] = entries[i].workshop_id;
	}
	Dictionary out;
	out["track_ids"] = content_ids;
	out["track_gameplay_digests"] = gameplay_digests;
	out["track_package_digests"] = package_digests;
	out["track_workshop_ids"] = workshop_ids;
	return out;
}

bool MxtTrackContentEvidence::load_metadata_dictionary(const Dictionary &value) {
	Array content_ids;
	Array gameplay_digests;
	Array package_digests;
	Array workshop_ids;
	if (!dictionary_array(value, "track_ids", content_ids) ||
			!dictionary_array(value, "track_gameplay_digests", gameplay_digests) ||
			!dictionary_array(value, "track_package_digests", package_digests) ||
			!dictionary_array(value, "track_workshop_ids", workshop_ids) ||
			content_ids.size() != gameplay_digests.size() || content_ids.size() != package_digests.size() ||
			content_ids.size() != workshop_ids.size() || content_ids.size() > MAX_TRACKS) {
		last_error = "Malformed track content evidence metadata.";
		return false;
	}
	std::vector<Entry> decoded;
	decoded.reserve(content_ids.size());
	for (int64_t i = 0; i < content_ids.size(); ++i) {
		const String content_id = content_ids[i];
		const String gameplay_digest = gameplay_digests[i];
		const String package_digest = package_digests[i];
		const String workshop_id = workshop_ids[i];
		if (content_id.utf8().length() > MAX_FIELD_BYTES || gameplay_digest.utf8().length() > MAX_FIELD_BYTES ||
				package_digest.utf8().length() > MAX_FIELD_BYTES || workshop_id.utf8().length() > MAX_FIELD_BYTES) {
			last_error = "Track content evidence metadata exceeds its bounded limits.";
			return false;
		}
		decoded.push_back({content_id, gameplay_digest, package_digest, workshop_id});
	}
	entries.swap(decoded);
	last_error = String();
	return true;
}
