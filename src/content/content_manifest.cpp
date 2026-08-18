#include "content/content_manifest.h"

#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <cmath>
#include <cstring>

using namespace godot;

namespace mxt::content {
namespace {

static void add_error(std::vector<String> &errors, const String &message)
{
	errors.push_back(message);
}

static bool is_json_space(uint8_t byte)
{
	return byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n';
}

static bool is_valid_utf8(const uint8_t *bytes, size_t size)
{
	for (size_t i = 0; i < size;) {
		const uint8_t first = bytes[i++];
		if (first <= 0x7f) {
			continue;
		}
		uint32_t codepoint = 0;
		uint32_t continuation_count = 0;
		uint32_t minimum = 0;
		if (first >= 0xc2 && first <= 0xdf) {
			codepoint = first & 0x1f;
			continuation_count = 1;
			minimum = 0x80;
		} else if (first >= 0xe0 && first <= 0xef) {
			codepoint = first & 0x0f;
			continuation_count = 2;
			minimum = 0x800;
		} else if (first >= 0xf0 && first <= 0xf4) {
			codepoint = first & 0x07;
			continuation_count = 3;
			minimum = 0x10000;
		} else {
			return false;
		}
		if (continuation_count > size - i) {
			return false;
		}
		for (uint32_t j = 0; j < continuation_count; ++j) {
			const uint8_t next = bytes[i++];
			if ((next & 0xc0) != 0x80) {
				return false;
			}
			codepoint = (codepoint << 6) | (next & 0x3f);
		}
		if (codepoint < minimum || codepoint > 0x10ffff ||
				(codepoint >= 0xd800 && codepoint <= 0xdfff)) {
			return false;
		}
	}
	return true;
}

class JsonMemberAudit {
private:
	const uint8_t *bytes;
	size_t size;
	size_t cursor = 0;
	std::vector<String> &errors;

	void skip_space()
	{
		while (cursor < size && is_json_space(bytes[cursor])) {
			++cursor;
		}
	}

	bool scan_string(size_t &out_start, size_t &out_end)
	{
		if (cursor >= size || bytes[cursor] != '"') {
			return false;
		}
		out_start = cursor++;
		while (cursor < size) {
			const uint8_t byte = bytes[cursor++];
			if (byte == '"') {
				out_end = cursor;
				return true;
			}
			if (byte == '\\') {
				if (cursor >= size) {
					return false;
				}
				++cursor;
			}
		}
		return false;
	}

	bool parse_value(uint32_t depth)
	{
		if (depth > 32) {
			add_error(errors, "manifest JSON nesting exceeds 32 levels");
			return false;
		}
		skip_space();
		if (cursor >= size) {
			return false;
		}
		if (bytes[cursor] == '{') {
			return parse_object(depth + 1);
		}
		if (bytes[cursor] == '[') {
			return parse_array(depth + 1);
		}
		if (bytes[cursor] == '"') {
			size_t start = 0;
			size_t end = 0;
			return scan_string(start, end);
		}
		const size_t start = cursor;
		while (cursor < size && bytes[cursor] != ',' && bytes[cursor] != ']' &&
				bytes[cursor] != '}' && !is_json_space(bytes[cursor])) {
			++cursor;
		}
		return cursor > start;
	}

	bool parse_array(uint32_t depth)
	{
		++cursor;
		skip_space();
		if (cursor < size && bytes[cursor] == ']') {
			++cursor;
			return true;
		}
		for (;;) {
			if (!parse_value(depth)) {
				return false;
			}
			skip_space();
			if (cursor < size && bytes[cursor] == ']') {
				++cursor;
				return true;
			}
			if (cursor >= size || bytes[cursor++] != ',') {
				return false;
			}
		}
	}

	bool parse_object(uint32_t depth)
	{
		++cursor;
		std::vector<String> keys;
		skip_space();
		if (cursor < size && bytes[cursor] == '}') {
			++cursor;
			return true;
		}
		for (;;) {
			skip_space();
			size_t key_start = 0;
			size_t key_end = 0;
			if (!scan_string(key_start, key_end)) {
				return false;
			}
			const String encoded_key = String::utf8(
					reinterpret_cast<const char *>(bytes + key_start),
					static_cast<int64_t>(key_end - key_start));
			const Variant decoded_key = JSON::parse_string(encoded_key);
			if (decoded_key.get_type() != Variant::STRING) {
				return false;
			}
			const String key = decoded_key;
			for (const String &existing : keys) {
				if (existing == key) {
					add_error(errors, String("manifest JSON contains duplicate member '") + key + String("'"));
					return false;
				}
			}
			keys.push_back(key);
			skip_space();
			if (cursor >= size || bytes[cursor++] != ':') {
				return false;
			}
			if (!parse_value(depth)) {
				return false;
			}
			skip_space();
			if (cursor < size && bytes[cursor] == '}') {
				++cursor;
				return true;
			}
			if (cursor >= size || bytes[cursor++] != ',') {
				return false;
			}
		}
	}

public:
	JsonMemberAudit(const PackedByteArray &input, std::vector<String> &out_errors) :
			bytes(input.ptr()), size(static_cast<size_t>(input.size())), errors(out_errors)
	{
	}

	bool run()
	{
		if (!parse_value(0)) {
			if (errors.empty()) {
				add_error(errors, "manifest JSON structure could not be audited");
			}
			return false;
		}
		skip_space();
		if (cursor != size) {
			add_error(errors, "manifest JSON contains trailing data");
			return false;
		}
		return true;
	}
};

static bool dictionary_has_only(
		const Dictionary &dictionary,
		const char *const *recognized,
		size_t recognized_count,
		const String &scope,
		std::vector<String> &errors)
{
	bool valid = true;
	const Array keys = dictionary.keys();
	for (int64_t i = 0; i < keys.size(); ++i) {
		if (keys[i].get_type() != Variant::STRING) {
			add_error(errors, scope + String(" contains a non-string member name"));
			valid = false;
			continue;
		}
		const String key = keys[i];
		bool found = false;
		for (size_t j = 0; j < recognized_count; ++j) {
			if (key == recognized[j]) {
				found = true;
				break;
			}
		}
		if (!found) {
			add_error(errors, scope + String(" contains unrecognized member '") + key + String("'"));
			valid = false;
		}
	}
	return valid;
}

static bool get_required_string(
		const Dictionary &dictionary,
		const char *key,
		int64_t max_length,
		String &out_value,
		std::vector<String> &errors,
		bool allow_empty = false)
{
	if (!dictionary.has(key)) {
		add_error(errors, String("manifest is missing required member '") + key + String("'"));
		return false;
	}
	const Variant value = dictionary[key];
	if (value.get_type() != Variant::STRING) {
		add_error(errors, String("manifest member '") + key + String("' must be a string"));
		return false;
	}
	out_value = value;
	if (out_value.is_empty() && !allow_empty) {
		add_error(errors, String("manifest member '") + key + String("' may not be empty"));
		return false;
	}
	if (out_value.length() > max_length) {
		add_error(errors, String("manifest member '") + key + String("' exceeds its length limit"));
		return false;
	}
	for (int64_t i = 0; i < out_value.length(); ++i) {
		const char32_t codepoint = out_value[i];
		if (codepoint == 0 || codepoint < 0x20 || codepoint == 0x7f) {
			add_error(errors, String("manifest member '") + key + String("' contains a control character"));
			return false;
		}
	}
	return true;
}

static bool is_lower_sha256(const String &value)
{
	if (value.length() != 64) {
		return false;
	}
	for (int64_t i = 0; i < value.length(); ++i) {
		const char32_t c = value[i];
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
			return false;
		}
	}
	return true;
}

static bool add_payload_file(
		const Dictionary &hashes,
		const String &path,
		ContentManifest &manifest,
		std::vector<String> &errors)
{
	if (!hashes.has(path)) {
		add_error(errors, "payload_sha256 is missing '" + path + "'");
		return false;
	}
	const Variant hash_value = hashes[path];
	if (hash_value.get_type() != Variant::STRING || !is_lower_sha256(hash_value)) {
		add_error(errors, "payload_sha256['" + path + "'] must be 64 lowercase hexadecimal characters");
		return false;
	}
	manifest.files.push_back({path, static_cast<String>(hash_value)});
	return true;
}

static bool require_exact_payload_path(
		const Dictionary &payload,
		const char *field,
		const char *expected,
		bool required,
		std::vector<String> &errors)
{
	if (!payload.has(field)) {
		if (required) {
		add_error(errors, String("payload is missing required member '") + field + String("'"));
			return false;
		}
		return true;
	}
	const Variant value = payload[field];
	if (value.get_type() != Variant::STRING || static_cast<String>(value) != expected) {
		add_error(errors, String("payload member '") + field + String("' must be exactly '") + expected + String("'"));
		return false;
	}
	return true;
}

} // namespace

String content_type_name(ContentType type)
{
	switch (type) {
	case ContentType::VEHICLE:
		return "vehicle";
	case ContentType::TRACK:
		return "track";
	default:
		return String();
	}
}

bool parse_manifest(
		const PackedByteArray &bytes,
		ContentManifest &out_manifest,
		std::vector<String> &out_errors)
{
	out_manifest = ContentManifest();
	if (bytes.is_empty()) {
		add_error(out_errors, "manifest.json is empty");
		return false;
	}
	if (static_cast<uint64_t>(bytes.size()) > MANIFEST_MAX_BYTES) {
		add_error(out_errors, "manifest.json exceeds 64 KiB");
		return false;
	}

	const String text = String::utf8(reinterpret_cast<const char *>(bytes.ptr()), bytes.size());
	Ref<JSON> json;
	json.instantiate();
	const Error parse_error = json->parse(text);
	if (parse_error != OK) {
		add_error(
				out_errors,
				String("manifest.json parse error on line ") + String::num_int64(json->get_error_line()) +
						": " + json->get_error_message());
		return false;
	}
	if (!audit_json_members(bytes, out_errors)) {
		return false;
	}
	const Variant parsed = json->get_data();
	if (parsed.get_type() != Variant::DICTIONARY) {
		add_error(out_errors, "manifest.json root must be an object");
		return false;
	}
	const Dictionary root = parsed;
	static const char *ROOT_KEYS[] = {
		"format_revision", "content_type", "title", "description", "author_name", "payload", "payload_sha256"
	};
	dictionary_has_only(root, ROOT_KEYS, std::size(ROOT_KEYS), "manifest", out_errors);

	if (!root.has("format_revision")) {
		add_error(out_errors, "manifest is missing required member 'format_revision'");
	} else {
		const Variant revision = root["format_revision"];
		double number = 0.0;
		if (revision.get_type() == Variant::INT) {
			number = static_cast<int64_t>(revision);
		} else if (revision.get_type() == Variant::FLOAT) {
			number = static_cast<double>(revision);
		} else {
			add_error(out_errors, "format_revision must be an integer");
		}
		if ((revision.get_type() == Variant::INT || revision.get_type() == Variant::FLOAT) &&
				(number != std::floor(number) || number != PACKAGE_FORMAT_REVISION)) {
			add_error(out_errors, "unsupported package format_revision; expected 1");
		} else if (number == PACKAGE_FORMAT_REVISION) {
			out_manifest.format_revision = PACKAGE_FORMAT_REVISION;
		}
	}

	String type_name;
	if (get_required_string(root, "content_type", 16, type_name, out_errors)) {
		if (type_name == "vehicle") {
			out_manifest.content_type = ContentType::VEHICLE;
		} else if (type_name == "track") {
			out_manifest.content_type = ContentType::TRACK;
		} else {
			add_error(out_errors, "content_type must be 'vehicle' or 'track'");
		}
	}
	get_required_string(root, "title", 128, out_manifest.title, out_errors);
	get_required_string(root, "description", 8000, out_manifest.description, out_errors, true);
	get_required_string(root, "author_name", 64, out_manifest.author_name, out_errors);

	if (!root.has("payload") || root["payload"].get_type() != Variant::DICTIONARY) {
		add_error(out_errors, "manifest member 'payload' must be an object");
		return false;
	}
	if (!root.has("payload_sha256") || root["payload_sha256"].get_type() != Variant::DICTIONARY) {
		add_error(out_errors, "manifest member 'payload_sha256' must be an object");
		return false;
	}
	const Dictionary payload = root["payload"];
	const Dictionary hashes = root["payload_sha256"];

	if (out_manifest.content_type == ContentType::VEHICLE) {
		static const char *VEHICLE_KEYS[] = {"model", "properties", "visual_metadata", "authoring"};
		dictionary_has_only(payload, VEHICLE_KEYS, std::size(VEHICLE_KEYS), "payload", out_errors);
		require_exact_payload_path(payload, "model", "vehicle/model.glb", true, out_errors);
		require_exact_payload_path(payload, "properties", "vehicle/properties.mxt_car_props", true, out_errors);
		require_exact_payload_path(payload, "visual_metadata", "vehicle/visual.json", true, out_errors);
		require_exact_payload_path(payload, "authoring", "vehicle/authoring.json", true, out_errors);
		add_payload_file(hashes, "vehicle/model.glb", out_manifest, out_errors);
		add_payload_file(hashes, "vehicle/properties.mxt_car_props", out_manifest, out_errors);
		add_payload_file(hashes, "vehicle/visual.json", out_manifest, out_errors);
		add_payload_file(hashes, "vehicle/authoring.json", out_manifest, out_errors);
		out_manifest.authoritative_path = "vehicle/properties.mxt_car_props";
	} else if (out_manifest.content_type == ContentType::TRACK) {
		static const char *TRACK_KEYS[] = {"track", "visual", "metadata"};
		dictionary_has_only(payload, TRACK_KEYS, std::size(TRACK_KEYS), "payload", out_errors);
		require_exact_payload_path(payload, "track", "track/track.mxt_track", true, out_errors);
		require_exact_payload_path(payload, "visual", "track/visual.glb", true, out_errors);
		require_exact_payload_path(payload, "metadata", "track/metadata.json", true, out_errors);
		add_payload_file(hashes, "track/track.mxt_track", out_manifest, out_errors);
		add_payload_file(hashes, "track/visual.glb", out_manifest, out_errors);
		add_payload_file(hashes, "track/metadata.json", out_manifest, out_errors);
		out_manifest.authoritative_path = "track/track.mxt_track";
	}
	add_payload_file(hashes, "preview.png", out_manifest, out_errors);

	if (hashes.size() != static_cast<int64_t>(out_manifest.files.size())) {
		add_error(out_errors, "payload_sha256 must contain exactly one hash for every declared payload file and preview.png");
	}
	return out_errors.empty();
}

bool audit_json_members(const PackedByteArray &bytes, std::vector<String> &out_errors)
{
	if (!is_valid_utf8(bytes.ptr(), static_cast<size_t>(bytes.size()))) {
		add_error(out_errors, "JSON input is not valid UTF-8");
		return false;
	}
	return JsonMemberAudit(bytes, out_errors).run();
}

Dictionary manifest_to_dictionary(const ContentManifest &manifest)
{
	Dictionary output;
	output["format_revision"] = manifest.format_revision;
	output["content_type"] = content_type_name(manifest.content_type);
	output["title"] = manifest.title;
	output["description"] = manifest.description;
	output["author_name"] = manifest.author_name;
	output["authoritative_path"] = manifest.authoritative_path;
	Array files;
	for (const ManifestFile &file : manifest.files) {
		Dictionary item;
		item["path"] = file.path;
		item["sha256"] = file.declared_sha256;
		files.push_back(item);
	}
	output["files"] = files;
	return output;
}

} // namespace mxt::content
