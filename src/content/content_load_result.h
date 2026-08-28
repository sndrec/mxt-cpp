#pragma once

#include "content/content_record.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <vector>

namespace godot {

class MxtContentLoadResult : public RefCounted {
	GDCLASS(MxtContentLoadResult, RefCounted)

public:
	enum ResultCode {
		RESULT_OK = 0,
		RESULT_INVALID_INPUT,
		RESULT_IO_ERROR,
		RESULT_VALIDATION_FAILED,
	};

	struct Diagnostic {
		String path;
		std::vector<String> errors;
	};

private:
	ResultCode code = RESULT_OK;
	Ref<MxtContentRecord> record;
	std::vector<String> errors;
	std::vector<Diagnostic> diagnostics;
	Dictionary validation_profile;
	String package_path;
	String package_digest;
	String gameplay_digest;
	bool reused_existing = false;
	int32_t registered_count = 0;

protected:
	static void _bind_methods();

public:
	void set_code(ResultCode value) { code = value; }
	void set_record(const mxt::content::ContentRecord &value);
	void set_errors(const std::vector<String> &value);
	void add_diagnostic(const String &path, const std::vector<String> &value);
	void set_validation_profile(const Dictionary &value) { validation_profile = value; }
	void set_package_path(const String &value) { package_path = value; }
	void set_package_digest(const String &value) { package_digest = value; }
	void set_gameplay_digest(const String &value) { gameplay_digest = value; }
	void set_reused_existing(bool value) { reused_existing = value; }
	void set_registered_count(int32_t value) { registered_count = value; }

	bool is_valid() const { return code == RESULT_OK; }
	int32_t get_code() const { return static_cast<int32_t>(code); }
	Ref<MxtContentRecord> get_record() const { return record; }
	PackedStringArray get_errors() const;
	Dictionary get_validation_profile() const { return validation_profile.duplicate(true); }
	String get_package_path() const { return package_path; }
	String get_package_digest() const { return package_digest; }
	String get_gameplay_digest() const { return gameplay_digest; }
	bool get_reused_existing() const { return reused_existing; }
	int32_t get_registered_count() const { return registered_count; }
	int32_t get_diagnostic_count() const { return static_cast<int32_t>(diagnostics.size()); }
	String get_diagnostic_path(int32_t index) const;
	PackedStringArray get_diagnostic_errors(int32_t index) const;
};

} // namespace godot

VARIANT_ENUM_CAST(godot::MxtContentLoadResult::ResultCode)
