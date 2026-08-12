#include "content/glb_validator.h"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/gltf_document.hpp>
#include <godot_cpp/classes/gltf_state.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

using namespace godot;

namespace mxt::content {
namespace {

static constexpr uint64_t MAX_GLB_JSON_BYTES = 16u * 1024u * 1024u;
static constexpr uint32_t MAX_TEXTURE_DIMENSION = 4096;
static constexpr uint32_t MAX_HIERARCHY_DEPTH = 64;

struct GlbBudgets {
	uint32_t nodes;
	uint32_t meshes;
	uint32_t primitives;
	uint32_t materials;
	uint32_t images;
	uint64_t vertices;
	uint64_t triangles;
	uint64_t texture_pixels;
};

struct BufferViewInfo {
	uint64_t offset = 0;
	uint64_t length = 0;
	uint32_t stride = 0;
};

struct AccessorInfo {
	uint64_t count = 0;
	uint32_t component_type = 0;
	uint32_t component_count = 0;
};

static GlbBudgets budgets_for(ContentType type)
{
	if (type == ContentType::VEHICLE) {
		return {1024, 256, 1024, 256, VEHICLE_MODEL_MAX_IMAGES,
			VEHICLE_MODEL_MAX_VERTICES, VEHICLE_MODEL_MAX_TRIANGLES, VEHICLE_MODEL_MAX_TEXTURE_PIXELS};
	}
	return {65'536, 8192, 32'768, 2048, 512, 8'000'000, 2'000'000, 256u * 1024u * 1024u};
}

static void add_error(std::vector<String> &errors, const String &path, const String &message)
{
	errors.push_back("GLB '" + path + "': " + message);
}

static uint32_t read_le_u32(const uint8_t *bytes)
{
	return static_cast<uint32_t>(bytes[0]) |
			(static_cast<uint32_t>(bytes[1]) << 8) |
			(static_cast<uint32_t>(bytes[2]) << 16) |
			(static_cast<uint32_t>(bytes[3]) << 24);
}

static uint32_t read_be_u32(const uint8_t *bytes)
{
	return (static_cast<uint32_t>(bytes[0]) << 24) |
			(static_cast<uint32_t>(bytes[1]) << 16) |
			(static_cast<uint32_t>(bytes[2]) << 8) |
			static_cast<uint32_t>(bytes[3]);
}

static int64_t json_integer(const Variant &value)
{
	if (value.get_type() == Variant::INT) {
		return static_cast<int64_t>(value);
	}
	if (value.get_type() == Variant::FLOAT) {
		const double number = static_cast<double>(value);
		if (std::isfinite(number) && number == std::floor(number) && number >= 0.0 && number <= 9.0e15) {
			return static_cast<int64_t>(number);
		}
	}
	return -1;
}

static bool get_optional_array(const Dictionary &object, const char *key, Array &out)
{
	if (!object.has(key)) {
		out = Array();
		return true;
	}
	const Variant value = object[key];
	if (value.get_type() != Variant::ARRAY) {
		return false;
	}
	out = value;
	return true;
}

static bool checked_range(uint64_t offset, uint64_t length, uint64_t container_length)
{
	return offset <= container_length && length <= container_length - offset;
}

static bool validate_numeric_array(const Variant &value, int64_t expected_size)
{
	if (value.get_type() != Variant::ARRAY) {
		return false;
	}
	const Array array = value;
	if (array.size() != expected_size) {
		return false;
	}
	for (int64_t i = 0; i < array.size(); ++i) {
		if (array[i].get_type() != Variant::FLOAT && array[i].get_type() != Variant::INT) {
			return false;
		}
		const double number = array[i].get_type() == Variant::FLOAT
				? static_cast<double>(array[i])
				: static_cast<double>(static_cast<int64_t>(array[i]));
		if (!std::isfinite(number)) {
			return false;
		}
	}
	return true;
}

static bool reject_nested_extensions(const Variant &value, uint32_t depth)
{
	if (depth > 64) {
		return false;
	}
	if (value.get_type() == Variant::DICTIONARY) {
		const Dictionary object = value;
		if (object.has("extensions")) {
			const Variant extensions = object["extensions"];
			if (extensions.get_type() != Variant::DICTIONARY || !static_cast<Dictionary>(extensions).is_empty()) {
				return false;
			}
		}
		const Array values = object.values();
		for (int64_t i = 0; i < values.size(); ++i) {
			if (!reject_nested_extensions(values[i], depth + 1)) {
				return false;
			}
		}
	} else if (value.get_type() == Variant::ARRAY) {
		const Array array = value;
		for (int64_t i = 0; i < array.size(); ++i) {
			if (!reject_nested_extensions(array[i], depth + 1)) {
				return false;
			}
		}
	}
	return true;
}

static bool parse_image_dimensions(
		const PackedByteArray &bytes,
		const String &mime_type,
		uint32_t &out_width,
		uint32_t &out_height)
{
	out_width = 0;
	out_height = 0;
	const uint8_t *data = bytes.ptr();
	const uint64_t size = static_cast<uint64_t>(bytes.size());
	if (mime_type == "image/png") {
		static const uint8_t PNG_SIGNATURE[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
		if (size < 24 || std::memcmp(data, PNG_SIGNATURE, 8) != 0 ||
				read_be_u32(data + 8) != 13 || std::memcmp(data + 12, "IHDR", 4) != 0) {
			return false;
		}
		out_width = read_be_u32(data + 16);
		out_height = read_be_u32(data + 20);
		return true;
	}
	if (mime_type != "image/jpeg" || size < 4 || data[0] != 0xff || data[1] != 0xd8) {
		return false;
	}
	uint64_t cursor = 2;
	while (cursor + 4 <= size) {
		while (cursor < size && data[cursor] != 0xff) {
			++cursor;
		}
		while (cursor < size && data[cursor] == 0xff) {
			++cursor;
		}
		if (cursor >= size) {
			return false;
		}
		const uint8_t marker = data[cursor++];
		if (marker == 0xd8 || marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7)) {
			continue;
		}
		if (marker == 0xd9 || marker == 0xda || cursor + 2 > size) {
			return false;
		}
		const uint32_t segment_length = (static_cast<uint32_t>(data[cursor]) << 8) | data[cursor + 1];
		if (segment_length < 2 || !checked_range(cursor, segment_length, size)) {
			return false;
		}
		const bool is_sof = (marker >= 0xc0 && marker <= 0xcf &&
				marker != 0xc4 && marker != 0xc8 && marker != 0xcc);
		if (is_sof) {
			if (segment_length < 7) {
				return false;
			}
			out_height = (static_cast<uint32_t>(data[cursor + 3]) << 8) | data[cursor + 4];
			out_width = (static_cast<uint32_t>(data[cursor + 5]) << 8) | data[cursor + 6];
			return true;
		}
		cursor += segment_length;
	}
	return false;
}

static uint32_t component_size(uint32_t component_type)
{
	switch (component_type) {
	case 5120:
	case 5121:
		return 1;
	case 5122:
	case 5123:
		return 2;
	case 5125:
	case 5126:
		return 4;
	default:
		return 0;
	}
}

static uint32_t type_component_count(const String &type)
{
	if (type == "SCALAR") return 1;
	if (type == "VEC2") return 2;
	if (type == "VEC3") return 3;
	if (type == "VEC4" || type == "MAT2") return 4;
	if (type == "MAT3") return 9;
	if (type == "MAT4") return 16;
	return 0;
}

} // namespace

bool validate_glb_file(
		const String &path,
		ContentType content_type,
		std::vector<String> &out_errors,
		VehicleGlbInfo *out_vehicle_info)
{
	if (out_vehicle_info) *out_vehicle_info = VehicleGlbInfo();
	const GlbBudgets budget = budgets_for(content_type);
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
	if (file.is_null() || file->get_length() < 20) {
		add_error(out_errors, path, "file is missing or shorter than its header");
		return false;
	}
	const uint64_t file_size = file->get_length();
	if (out_vehicle_info) out_vehicle_info->file_bytes = file_size;
	const PackedByteArray header = file->get_buffer(12);
	if (header.size() != 12 || std::memcmp(header.ptr(), "glTF", 4) != 0 ||
			read_le_u32(header.ptr() + 4) != 2 || read_le_u32(header.ptr() + 8) != file_size) {
		add_error(out_errors, path, "header is invalid or its declared length does not match");
		return false;
	}

	uint64_t cursor = 12;
	uint64_t json_offset = 0;
	uint32_t json_length = 0;
	uint64_t bin_offset = 0;
	uint32_t bin_length = 0;
	uint32_t chunk_index = 0;
	while (cursor < file_size) {
		if (file_size - cursor < 8) {
			add_error(out_errors, path, "chunk header is truncated");
			return false;
		}
		file->seek(cursor);
		const PackedByteArray chunk_header = file->get_buffer(8);
		const uint32_t chunk_length = read_le_u32(chunk_header.ptr());
		const uint32_t chunk_type = read_le_u32(chunk_header.ptr() + 4);
		if ((chunk_length & 3u) != 0 || !checked_range(cursor + 8, chunk_length, file_size)) {
			add_error(out_errors, path, "chunk length is invalid");
			return false;
		}
		if (chunk_index == 0 && chunk_type == 0x4e4f534au && chunk_length > 0) {
			json_offset = cursor + 8;
			json_length = chunk_length;
		} else if (chunk_index == 1 && chunk_type == 0x004e4942u) {
			bin_offset = cursor + 8;
			bin_length = chunk_length;
		} else {
			add_error(out_errors, path, "contains a duplicate or unsupported chunk");
			return false;
		}
		cursor += 8u + chunk_length;
		++chunk_index;
	}
	if (json_offset == 0 || cursor != file_size || json_length > MAX_GLB_JSON_BYTES) {
		add_error(out_errors, path, "JSON chunk is missing, oversized, or does not cover the file correctly");
		return false;
	}

	file->seek(json_offset);
	PackedByteArray json_bytes = file->get_buffer(json_length);
	while (!json_bytes.is_empty() &&
			(json_bytes[json_bytes.size() - 1] == ' ' || json_bytes[json_bytes.size() - 1] == 0)) {
		json_bytes.resize(json_bytes.size() - 1);
	}
	std::vector<String> json_errors;
	if (!audit_json_members(json_bytes, json_errors)) {
		add_error(out_errors, path, "JSON contains duplicate members or invalid structure");
		return false;
	}
	Ref<JSON> json;
	json.instantiate();
	const String json_text = String::utf8(reinterpret_cast<const char *>(json_bytes.ptr()), json_bytes.size());
	if (json->parse(json_text) != OK || json->get_data().get_type() != Variant::DICTIONARY) {
		add_error(out_errors, path, "JSON chunk is invalid");
		return false;
	}
	const Dictionary root = json->get_data();
	if (!root.has("asset") || root["asset"].get_type() != Variant::DICTIONARY) {
		add_error(out_errors, path, "asset metadata is missing or invalid");
		return false;
	}
	const Dictionary asset = root["asset"];
	if (!asset.has("version") || asset["version"].get_type() != Variant::STRING ||
			static_cast<String>(asset["version"]) != "2.0") {
		add_error(out_errors, path, "asset.version must be exactly 2.0");
		return false;
	}
	if (!reject_nested_extensions(root, 0)) {
		add_error(out_errors, path, "extensions are not supported in package revision 1");
		return false;
	}
	Array extensions_used;
	Array extensions_required;
	if (!get_optional_array(root, "extensionsUsed", extensions_used) ||
			!get_optional_array(root, "extensionsRequired", extensions_required)) {
		add_error(out_errors, path, "extensionsUsed and extensionsRequired must be arrays");
		return false;
	}
	for (int64_t i = 0; i < extensions_used.size(); ++i) {
		if (extensions_used[i].get_type() != Variant::STRING ||
				static_cast<String>(extensions_used[i]) != "GODOT_single_root") {
			add_error(out_errors, path, "extensionsUsed contains an unsupported extension");
			return false;
		}
	}
	if (!extensions_required.is_empty()) {
		add_error(out_errors, path, "required glTF extensions are unsupported");
		return false;
	}
	for (const char *unsupported : {"animations", "skins", "cameras"}) {
		Array array;
		if (!get_optional_array(root, unsupported, array) || !array.is_empty()) {
			add_error(out_errors, path, String("unsupported glTF feature '") + unsupported + String("'"));
			return false;
		}
	}

	Array buffers;
	Array buffer_views;
	Array accessors;
	Array nodes;
	Array meshes;
	Array scenes;
	Array materials;
	Array images;
	Array textures;
	if (!get_optional_array(root, "buffers", buffers) || buffers.size() != 1 ||
			!get_optional_array(root, "bufferViews", buffer_views) ||
			!get_optional_array(root, "accessors", accessors) ||
			!get_optional_array(root, "nodes", nodes) ||
			!get_optional_array(root, "meshes", meshes) ||
			!get_optional_array(root, "scenes", scenes) ||
			!get_optional_array(root, "materials", materials) ||
			!get_optional_array(root, "images", images) ||
			!get_optional_array(root, "textures", textures)) {
		add_error(out_errors, path, "required glTF tables have invalid types or buffer count is not one");
		return false;
	}
	if (nodes.is_empty() || meshes.is_empty() || scenes.size() != 1) {
		add_error(out_errors, path, "revision 1 requires nodes, meshes, and exactly one scene");
		return false;
	}
	const int64_t active_scene = root.has("scene") ? json_integer(root["scene"]) : 0;
	if (active_scene != 0 || scenes[0].get_type() != Variant::DICTIONARY) {
		add_error(out_errors, path, "the single scene must be scene 0");
		return false;
	}
	if (nodes.size() > budget.nodes || meshes.size() > budget.meshes ||
			materials.size() > budget.materials || images.size() > budget.images) {
		add_error(out_errors, path, "node, mesh, material, or image count exceeds its content budget");
		return false;
	}
	if (buffers[0].get_type() != Variant::DICTIONARY) {
		add_error(out_errors, path, "buffer entry must be an object");
		return false;
	}
	const Dictionary buffer = buffers[0];
	if (buffer.has("uri")) {
		add_error(out_errors, path, "external and data URI buffers are not allowed");
		return false;
	}
	const int64_t declared_buffer_length = buffer.has("byteLength") ? json_integer(buffer["byteLength"]) : -1;
	if (declared_buffer_length < 0 || static_cast<uint64_t>(declared_buffer_length) > bin_length ||
			static_cast<uint64_t>(bin_length) - static_cast<uint64_t>(declared_buffer_length) > 3) {
		add_error(out_errors, path, "BIN chunk does not match buffers[0].byteLength");
		return false;
	}

	std::vector<BufferViewInfo> views(static_cast<size_t>(buffer_views.size()));
	for (int64_t i = 0; i < buffer_views.size(); ++i) {
		if (buffer_views[i].get_type() != Variant::DICTIONARY) {
			add_error(out_errors, path, "bufferView entry must be an object");
			return false;
		}
		const Dictionary view = buffer_views[i];
		const int64_t buffer_index = view.has("buffer") ? json_integer(view["buffer"]) : -1;
		const int64_t offset = view.has("byteOffset") ? json_integer(view["byteOffset"]) : 0;
		const int64_t length = view.has("byteLength") ? json_integer(view["byteLength"]) : -1;
		const int64_t stride = view.has("byteStride") ? json_integer(view["byteStride"]) : 0;
		if (buffer_index != 0 || offset < 0 || length < 0 || stride < 0 || stride > 252 ||
				!checked_range(static_cast<uint64_t>(offset), static_cast<uint64_t>(length), static_cast<uint64_t>(declared_buffer_length))) {
			add_error(out_errors, path, "bufferView is outside the BIN buffer or has invalid stride");
			return false;
		}
		views[static_cast<size_t>(i)] = {
			static_cast<uint64_t>(offset), static_cast<uint64_t>(length), static_cast<uint32_t>(stride)
		};
	}

	std::vector<AccessorInfo> accessor_info(static_cast<size_t>(accessors.size()));
	const uint64_t max_accessor_elements = content_type == ContentType::VEHICLE ? 1'000'000u : 8'000'000u;
	for (int64_t i = 0; i < accessors.size(); ++i) {
		if (accessors[i].get_type() != Variant::DICTIONARY) {
			add_error(out_errors, path, "accessor entry must be an object");
			return false;
		}
		const Dictionary accessor = accessors[i];
		if (accessor.has("sparse") || !accessor.has("bufferView") || !accessor.has("count") ||
				!accessor.has("componentType") || !accessor.has("type")) {
			add_error(out_errors, path, "sparse or bufferless accessors are not supported");
			return false;
		}
		const int64_t view_index = json_integer(accessor["bufferView"]);
		const int64_t count = json_integer(accessor["count"]);
		const int64_t component_type = json_integer(accessor["componentType"]);
		const int64_t byte_offset = accessor.has("byteOffset") ? json_integer(accessor["byteOffset"]) : 0;
		if (accessor["type"].get_type() != Variant::STRING || view_index < 0 || view_index >= buffer_views.size() ||
				count <= 0 || static_cast<uint64_t>(count) > max_accessor_elements || byte_offset < 0) {
			add_error(out_errors, path, "accessor has invalid indices, count, offset, or type");
			return false;
		}
		const uint32_t scalar_size = component_size(static_cast<uint32_t>(component_type));
		const uint32_t components = type_component_count(static_cast<String>(accessor["type"]));
		if (scalar_size == 0 || components == 0) {
			add_error(out_errors, path, "accessor componentType or type is unsupported");
			return false;
		}
		const uint64_t element_size = static_cast<uint64_t>(scalar_size) * components;
		const BufferViewInfo &view = views[static_cast<size_t>(view_index)];
		const uint64_t stride = view.stride == 0 ? element_size : view.stride;
		if (stride < element_size || stride % scalar_size != 0 ||
				static_cast<uint64_t>(byte_offset) > view.length ||
				static_cast<uint64_t>(count - 1) > (UINT64_MAX - static_cast<uint64_t>(byte_offset) - element_size) / stride ||
				static_cast<uint64_t>(byte_offset) + static_cast<uint64_t>(count - 1) * stride + element_size > view.length) {
			add_error(out_errors, path, "accessor byte range is outside its bufferView");
			return false;
		}
		accessor_info[static_cast<size_t>(i)] = {
			static_cast<uint64_t>(count), static_cast<uint32_t>(component_type), components
		};
	}
	for (int64_t material_index = 0; material_index < materials.size(); ++material_index) {
		if (materials[material_index].get_type() != Variant::DICTIONARY) {
			add_error(out_errors, path, "material entry must be an object");
			return false;
		}
		const Dictionary material = materials[material_index];
		auto validate_texture_info = [&](const Dictionary &owner, const char *key) {
			if (!owner.has(key)) return true;
			if (owner[key].get_type() != Variant::DICTIONARY) return false;
			const Dictionary info = owner[key];
			const int64_t texture_index = info.has("index") ? json_integer(info["index"]) : -1;
			const int64_t texcoord = info.has("texCoord") ? json_integer(info["texCoord"]) : 0;
			return texture_index >= 0 && texture_index < textures.size() && texcoord >= 0 && texcoord <= 1;
		};
		if (material.has("pbrMetallicRoughness")) {
			if (material["pbrMetallicRoughness"].get_type() != Variant::DICTIONARY) {
				add_error(out_errors, path, "material pbrMetallicRoughness must be an object");
				return false;
			}
			const Dictionary pbr = material["pbrMetallicRoughness"];
			if (!validate_texture_info(pbr, "baseColorTexture") ||
					!validate_texture_info(pbr, "metallicRoughnessTexture")) {
				add_error(out_errors, path, "material PBR texture input is invalid");
				return false;
			}
		}
		if (!validate_texture_info(material, "normalTexture") ||
				!validate_texture_info(material, "occlusionTexture") ||
				!validate_texture_info(material, "emissiveTexture")) {
			add_error(out_errors, path, "material texture input is invalid");
			return false;
		}
	}

	uint64_t total_vertices = 0;
	uint64_t total_triangles = 0;
	uint32_t total_primitives = 0;
	for (int64_t mesh_index = 0; mesh_index < meshes.size(); ++mesh_index) {
		if (meshes[mesh_index].get_type() != Variant::DICTIONARY) {
			add_error(out_errors, path, "mesh entry must be an object");
			return false;
		}
		const Dictionary mesh = meshes[mesh_index];
		Array primitives;
		if (!get_optional_array(mesh, "primitives", primitives) || primitives.is_empty()) {
			add_error(out_errors, path, "mesh must contain at least one primitive");
			return false;
		}
		total_primitives += static_cast<uint32_t>(primitives.size());
		if (total_primitives > budget.primitives) {
			add_error(out_errors, path, "primitive count exceeds its content budget");
			return false;
		}
		for (int64_t primitive_index = 0; primitive_index < primitives.size(); ++primitive_index) {
			if (primitives[primitive_index].get_type() != Variant::DICTIONARY) {
				add_error(out_errors, path, "primitive entry must be an object");
				return false;
			}
			const Dictionary primitive = primitives[primitive_index];
			const int64_t mode = primitive.has("mode") ? json_integer(primitive["mode"]) : 4;
			if (mode != 4 || primitive.has("targets") || !primitive.has("attributes") ||
					primitive["attributes"].get_type() != Variant::DICTIONARY) {
				add_error(out_errors, path, "only non-morphing TRIANGLES primitives are supported");
				return false;
			}
			const Dictionary attributes = primitive["attributes"];
			if (!attributes.has("POSITION")) {
				add_error(out_errors, path, "primitive is missing POSITION");
				return false;
			}
			const Array attribute_names = attributes.keys();
			for (int64_t attribute = 0; attribute < attribute_names.size(); ++attribute) {
				const String name = attribute_names[attribute];
				if (name != "POSITION" && name != "NORMAL" && name != "TANGENT" &&
						name != "TEXCOORD_0" && name != "TEXCOORD_1" && name != "COLOR_0") {
					add_error(out_errors, path, "primitive contains unsupported vertex attribute '" + name + "'");
					return false;
				}
				const int64_t accessor_index = json_integer(attributes[name]);
				if (accessor_index < 0 || accessor_index >= accessors.size()) {
					add_error(out_errors, path, "primitive attribute references an invalid accessor");
					return false;
				}
			}
			const int64_t position_accessor = json_integer(attributes["POSITION"]);
			const AccessorInfo &positions = accessor_info[static_cast<size_t>(position_accessor)];
			if (positions.component_type != 5126 || positions.component_count != 3) {
				add_error(out_errors, path, "POSITION must use floating-point VEC3 data");
				return false;
			}
			total_vertices += positions.count;
			uint64_t index_count = positions.count;
			if (primitive.has("indices")) {
				const int64_t index_accessor = json_integer(primitive["indices"]);
				if (index_accessor < 0 || index_accessor >= accessors.size()) {
					add_error(out_errors, path, "primitive indices reference an invalid accessor");
					return false;
				}
				const AccessorInfo &indices = accessor_info[static_cast<size_t>(index_accessor)];
				if (indices.component_count != 1 || (indices.component_type != 5121 &&
						indices.component_type != 5123 && indices.component_type != 5125)) {
					add_error(out_errors, path, "indices must use an unsigned scalar accessor");
					return false;
				}
				index_count = indices.count;
			}
			if (index_count % 3 != 0) {
				add_error(out_errors, path, "TRIANGLES primitive index/vertex count must be divisible by three");
				return false;
			}
			total_triangles += index_count / 3;
			int64_t material_index = -1;
			if (primitive.has("material")) {
				material_index = json_integer(primitive["material"]);
				if (material_index < 0 || material_index >= materials.size()) {
					add_error(out_errors, path, "primitive references an invalid material");
					return false;
				}
			}
			if (content_type == ContentType::VEHICLE && out_vehicle_info) {
				VehicleGlbSurface info;
				info.name = String("Surface ") + String::num_int64(primitive_index + 1);
				if (material_index >= 0) {
					info.material_index = static_cast<uint32_t>(material_index);
					const Dictionary material = materials[material_index];
					if (material.has("name") && material["name"].get_type() == Variant::STRING &&
							!static_cast<String>(material["name"]).is_empty()) {
						info.name += String(" - ") + static_cast<String>(material["name"]);
					}
					if (material.has("pbrMetallicRoughness") &&
							material["pbrMetallicRoughness"].get_type() == Variant::DICTIONARY) {
						const Dictionary pbr = material["pbrMetallicRoughness"];
						info.has_albedo_texture = pbr.has("baseColorTexture") &&
								pbr["baseColorTexture"].get_type() == Variant::DICTIONARY;
					}
					info.has_normal_texture = material.has("normalTexture") &&
							material["normalTexture"].get_type() == Variant::DICTIONARY;
					info.has_paint_mask_texture = material.has("occlusionTexture") &&
							material["occlusionTexture"].get_type() == Variant::DICTIONARY;
				}
				out_vehicle_info->surfaces.push_back(info);
			}
		}
	}
	if (total_vertices > budget.vertices || total_triangles > budget.triangles) {
		add_error(out_errors, path, "vertex or triangle count exceeds its content budget");
		return false;
	}
	if (out_vehicle_info) {
		out_vehicle_info->vertices = total_vertices;
		out_vehicle_info->triangles = total_triangles;
	}

	std::vector<int32_t> parent(static_cast<size_t>(nodes.size()), -1);
	uint32_t mesh_reference_count = 0;
	for (int64_t node_index = 0; node_index < nodes.size(); ++node_index) {
		if (nodes[node_index].get_type() != Variant::DICTIONARY) {
			add_error(out_errors, path, "node entry must be an object");
			return false;
		}
		const Dictionary node = nodes[node_index];
		if (node.has("camera") || node.has("skin") || node.has("weights")) {
			add_error(out_errors, path, "camera, skin, and morph-weight nodes are unsupported");
			return false;
		}
		if (node.has("mesh")) {
			const int64_t mesh = json_integer(node["mesh"]);
			if (mesh < 0 || mesh >= meshes.size()) {
				add_error(out_errors, path, "node references an invalid mesh");
				return false;
			}
			++mesh_reference_count;
		}
		if ((node.has("matrix") && !validate_numeric_array(node["matrix"], 16)) ||
				(node.has("translation") && !validate_numeric_array(node["translation"], 3)) ||
				(node.has("rotation") && !validate_numeric_array(node["rotation"], 4)) ||
				(node.has("scale") && !validate_numeric_array(node["scale"], 3)) ||
				(node.has("matrix") && (node.has("translation") || node.has("rotation") || node.has("scale")))) {
			add_error(out_errors, path, "node transform is invalid or mixes matrix and TRS forms");
			return false;
		}
		Array children;
		if (!get_optional_array(node, "children", children)) {
			add_error(out_errors, path, "node children must be an array");
			return false;
		}
		for (int64_t i = 0; i < children.size(); ++i) {
			const int64_t child = json_integer(children[i]);
			if (child < 0 || child >= nodes.size() || child == node_index || parent[static_cast<size_t>(child)] != -1) {
				add_error(out_errors, path, "node hierarchy has an invalid child or multiple parents");
				return false;
			}
			parent[static_cast<size_t>(child)] = static_cast<int32_t>(node_index);
		}
	}
	std::vector<uint8_t> visit(static_cast<size_t>(nodes.size()), 0);
	std::function<bool(int64_t, uint32_t)> visit_node = [&](int64_t index, uint32_t depth) {
		if (depth > MAX_HIERARCHY_DEPTH || visit[static_cast<size_t>(index)] == 1) return false;
		if (visit[static_cast<size_t>(index)] == 2) return true;
		visit[static_cast<size_t>(index)] = 1;
		const Dictionary node = nodes[index];
		Array children;
		get_optional_array(node, "children", children);
		for (int64_t i = 0; i < children.size(); ++i) {
			if (!visit_node(json_integer(children[i]), depth + 1)) return false;
		}
		visit[static_cast<size_t>(index)] = 2;
		return true;
	};
	for (int64_t i = 0; i < nodes.size(); ++i) {
		if (parent[static_cast<size_t>(i)] == -1 && !visit_node(i, 1)) {
			add_error(out_errors, path, "node hierarchy is cyclic or deeper than 64 levels");
			return false;
		}
	}
	for (uint8_t state : visit) {
		if (state != 2) {
			add_error(out_errors, path, "node hierarchy contains a cycle");
			return false;
		}
	}
	const Dictionary scene = scenes[0];
	Array scene_roots;
	if (!get_optional_array(scene, "nodes", scene_roots) || scene_roots.is_empty()) {
		add_error(out_errors, path, "scene 0 must list at least one root node");
		return false;
	}
	std::vector<uint8_t> listed_root(static_cast<size_t>(nodes.size()), 0);
	for (int64_t i = 0; i < scene_roots.size(); ++i) {
		const int64_t root_index = json_integer(scene_roots[i]);
		if (root_index < 0 || root_index >= nodes.size() ||
				parent[static_cast<size_t>(root_index)] != -1 || listed_root[static_cast<size_t>(root_index)] != 0) {
			add_error(out_errors, path, "scene 0 contains an invalid or duplicate root node");
			return false;
		}
		listed_root[static_cast<size_t>(root_index)] = 1;
	}
	for (int64_t i = 0; i < nodes.size(); ++i) {
		if (parent[static_cast<size_t>(i)] == -1 && listed_root[static_cast<size_t>(i)] == 0) {
			add_error(out_errors, path, "every hierarchy root must belong to scene 0");
			return false;
		}
	}
	if (mesh_reference_count == 0) {
		add_error(out_errors, path, "scene contains no mesh instances");
		return false;
	}
	if (content_type == ContentType::VEHICLE && (meshes.size() != 1 || mesh_reference_count != 1)) {
		add_error(out_errors, path, "vehicle scene must contain exactly one mesh and one mesh instance");
		return false;
	}

	uint64_t total_pixels = 0;
	for (int64_t image_index = 0; image_index < images.size(); ++image_index) {
		if (images[image_index].get_type() != Variant::DICTIONARY) {
			add_error(out_errors, path, "image entry must be an object");
			return false;
		}
		const Dictionary image = images[image_index];
		if (image.has("uri") || !image.has("bufferView") || !image.has("mimeType") ||
				image["mimeType"].get_type() != Variant::STRING) {
			add_error(out_errors, path, "images must be embedded bufferViews with an explicit MIME type");
			return false;
		}
		const int64_t view_index = json_integer(image["bufferView"]);
		const String mime_type = image["mimeType"];
		if (view_index < 0 || view_index >= buffer_views.size() ||
				(mime_type != "image/png" && mime_type != "image/jpeg")) {
			add_error(out_errors, path, "image bufferView or MIME type is unsupported");
			return false;
		}
		const BufferViewInfo &view = views[static_cast<size_t>(view_index)];
		if (view.length > 64u * 1024u * 1024u) {
			add_error(out_errors, path, "embedded image exceeds 64 MiB");
			return false;
		}
		file->seek(bin_offset + view.offset);
		const PackedByteArray image_bytes = file->get_buffer(static_cast<int64_t>(view.length));
		uint32_t width = 0;
		uint32_t height = 0;
		if (static_cast<uint64_t>(image_bytes.size()) != view.length ||
				!parse_image_dimensions(image_bytes, mime_type, width, height) ||
				width == 0 || height == 0 || width > MAX_TEXTURE_DIMENSION || height > MAX_TEXTURE_DIMENSION) {
			add_error(out_errors, path, "embedded image is malformed or exceeds 4096 pixels per dimension");
			return false;
		}
		total_pixels += static_cast<uint64_t>(width) * height;
		if (total_pixels > budget.texture_pixels) {
			add_error(out_errors, path, "total embedded texture pixels exceed the content budget");
			return false;
		}
	}
	for (int64_t texture_index = 0; texture_index < textures.size(); ++texture_index) {
		if (textures[texture_index].get_type() != Variant::DICTIONARY) {
			add_error(out_errors, path, "texture entry must be an object");
			return false;
		}
		const Dictionary texture = textures[texture_index];
		if (!texture.has("source")) {
			add_error(out_errors, path, "texture is missing its source image");
			return false;
		}
		const int64_t source = json_integer(texture["source"]);
		if (source < 0 || source >= images.size()) {
			add_error(out_errors, path, "texture references an invalid image");
			return false;
		}
	}
	if (out_vehicle_info) {
		out_vehicle_info->images = static_cast<uint32_t>(images.size());
		out_vehicle_info->texture_pixels = total_pixels;
	}
	Ref<GLTFDocument> document;
	document.instantiate();
	Ref<GLTFState> state;
	state.instantiate();
	if (document->append_from_file(path, state) != OK) {
		add_error(out_errors, path, "Godot rejected the bounded glTF document");
		return false;
	}
	return true;
}

} // namespace mxt::content
