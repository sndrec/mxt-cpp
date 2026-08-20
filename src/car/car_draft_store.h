#pragma once

#include "car/car_authoring_session.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {

class MxtCarDraftStore : public RefCounted {
	GDCLASS(MxtCarDraftStore, RefCounted)

  protected:
	static void _bind_methods();

  public:
	Array list_drafts() const;
	Dictionary save_draft(const String &draft_id, const Ref<MxtCarAuthoringSession> &session,
						  const Dictionary &metadata) const;
	Dictionary load_draft(const String &draft_id, const Ref<MxtCarAuthoringSession> &session) const;
	Dictionary duplicate_draft(const String &source_draft_id, const String &new_draft_id,
							   const String &new_title) const;
	Dictionary archive_draft(const String &draft_id) const;
	Dictionary delete_draft(const String &draft_id) const;
};

} // namespace godot
