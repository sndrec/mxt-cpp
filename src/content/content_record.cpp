#include "content/content_record.h"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

String MxtContentRecord::get_content_type_name() const {
	return mxt::content::content_type_name(record.content_type);
}

String MxtContentRecord::get_source_name() const {
	return mxt::content::content_source_name(record.source);
}

void MxtContentRecord::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_valid"), &MxtContentRecord::is_valid);

#define BIND_READONLY(type, name) \
	ClassDB::bind_method(D_METHOD("get_" #name), &MxtContentRecord::get_##name); \
	ADD_PROPERTY(PropertyInfo(type, #name), "", "get_" #name)
	BIND_READONLY(Variant::INT, content_type);
	BIND_READONLY(Variant::STRING, content_type_name);
	BIND_READONLY(Variant::INT, source);
	BIND_READONLY(Variant::STRING, source_name);
	BIND_READONLY(Variant::STRING, content_id);
	BIND_READONLY(Variant::STRING, package_digest);
	BIND_READONLY(Variant::STRING, gameplay_digest);
	BIND_READONLY(Variant::STRING, root_path);
	BIND_READONLY(Variant::STRING, preview_path);
	BIND_READONLY(Variant::STRING, authoritative_path);
	BIND_READONLY(Variant::STRING, visual_path);
	BIND_READONLY(Variant::STRING, metadata_path);
	BIND_READONLY(Variant::STRING, manual_boost_sfx_path);
	BIND_READONLY(Variant::STRING, albedo_texture_path);
	BIND_READONLY(Variant::STRING, normal_texture_path);
	BIND_READONLY(Variant::STRING, paint_mask_texture_path);
	BIND_READONLY(Variant::STRING, title);
	BIND_READONLY(Variant::STRING, description);
	BIND_READONLY(Variant::STRING, author_name);
	BIND_READONLY(Variant::DICTIONARY, visual_metadata);
	BIND_READONLY(Variant::DICTIONARY, authoring_metadata);
	BIND_READONLY(Variant::INT, published_file_id);
#undef BIND_READONLY

	BIND_ENUM_CONSTANT(CONTENT_INVALID);
	BIND_ENUM_CONSTANT(CONTENT_VEHICLE);
	BIND_ENUM_CONSTANT(CONTENT_TRACK);
	BIND_ENUM_CONSTANT(SOURCE_OFFICIAL);
	BIND_ENUM_CONSTANT(SOURCE_LOCAL_LOOSE);
	BIND_ENUM_CONSTANT(SOURCE_LOCAL_PACKAGE);
	BIND_ENUM_CONSTANT(SOURCE_LOCAL_DRAFT);
	BIND_ENUM_CONSTANT(SOURCE_WORKSHOP);
}
