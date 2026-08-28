#include "content/content_load_result.h"

#include <godot_cpp/core/class_db.hpp>

#include <algorithm>

using namespace godot;

namespace {
static constexpr size_t MAX_DIAGNOSTICS = 256;
static constexpr size_t MAX_MESSAGES = 64;

static PackedStringArray packed_messages(const std::vector<String> &messages) {
	PackedStringArray output;
	for (const String &message : messages) output.push_back(message);
	return output;
}
} // namespace

void MxtContentLoadResult::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_valid"), &MxtContentLoadResult::is_valid);
	ClassDB::bind_method(D_METHOD("get_code"), &MxtContentLoadResult::get_code);
	ClassDB::bind_method(D_METHOD("get_record"), &MxtContentLoadResult::get_record);
	ClassDB::bind_method(D_METHOD("get_errors"), &MxtContentLoadResult::get_errors);
	ClassDB::bind_method(D_METHOD("get_validation_profile"), &MxtContentLoadResult::get_validation_profile);
	ClassDB::bind_method(D_METHOD("get_package_path"), &MxtContentLoadResult::get_package_path);
	ClassDB::bind_method(D_METHOD("get_package_digest"), &MxtContentLoadResult::get_package_digest);
	ClassDB::bind_method(D_METHOD("get_gameplay_digest"), &MxtContentLoadResult::get_gameplay_digest);
	ClassDB::bind_method(D_METHOD("get_reused_existing"), &MxtContentLoadResult::get_reused_existing);
	ClassDB::bind_method(D_METHOD("get_registered_count"), &MxtContentLoadResult::get_registered_count);
	ClassDB::bind_method(D_METHOD("get_diagnostic_count"), &MxtContentLoadResult::get_diagnostic_count);
	ClassDB::bind_method(D_METHOD("get_diagnostic_path", "index"), &MxtContentLoadResult::get_diagnostic_path);
	ClassDB::bind_method(D_METHOD("get_diagnostic_errors", "index"), &MxtContentLoadResult::get_diagnostic_errors);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "code"), "", "get_code");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "record"), "", "get_record");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "errors"), "", "get_errors");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "validation_profile"), "", "get_validation_profile");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "package_path"), "", "get_package_path");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "package_digest"), "", "get_package_digest");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "gameplay_digest"), "", "get_gameplay_digest");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "reused_existing"), "", "get_reused_existing");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "registered_count"), "", "get_registered_count");

	BIND_ENUM_CONSTANT(RESULT_OK);
	BIND_ENUM_CONSTANT(RESULT_INVALID_INPUT);
	BIND_ENUM_CONSTANT(RESULT_IO_ERROR);
	BIND_ENUM_CONSTANT(RESULT_VALIDATION_FAILED);
}

void MxtContentLoadResult::set_record(const mxt::content::ContentRecord &value) {
	record.instantiate();
	record->set_record(value);
}

void MxtContentLoadResult::set_errors(const std::vector<String> &value) {
	const size_t count = std::min(value.size(), MAX_MESSAGES);
	errors.assign(value.begin(), value.begin() + count);
}

void MxtContentLoadResult::add_diagnostic(const String &path, const std::vector<String> &value) {
	if (diagnostics.size() >= MAX_DIAGNOSTICS) return;
	Diagnostic diagnostic;
	diagnostic.path = path;
	const size_t count = std::min(value.size(), MAX_MESSAGES);
	diagnostic.errors.assign(value.begin(), value.begin() + count);
	diagnostics.push_back(std::move(diagnostic));
}

PackedStringArray MxtContentLoadResult::get_errors() const {
	return packed_messages(errors);
}

String MxtContentLoadResult::get_diagnostic_path(int32_t index) const {
	return index >= 0 && static_cast<size_t>(index) < diagnostics.size() ? diagnostics[index].path : String();
}

PackedStringArray MxtContentLoadResult::get_diagnostic_errors(int32_t index) const {
	return index >= 0 && static_cast<size_t>(index) < diagnostics.size()
			? packed_messages(diagnostics[index].errors)
			: PackedStringArray();
}
