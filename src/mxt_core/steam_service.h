#ifndef MXT_STEAM_SERVICE_H
#define MXT_STEAM_SERVICE_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstdint>

namespace godot {

struct SteamWorkshopState;

class MxtSteamService : public Node {
	GDCLASS(MxtSteamService, Node)

private:
	friend struct SteamWorkshopState;

	String status_message;
	String persona_name;
	uint64_t steam_id = 0;
	uint32_t app_id = 0;
	bool initialized = false;
	bool init_attempted = false;
	int64_t next_request_id = 1;
	Array workshop_items;
	SteamWorkshopState *workshop_state = nullptr;

	void clear_account_state();
	void publish_status();
	void shutdown_steam_internal(bool publish_change);
	int64_t allocate_request_id();
	void complete_workshop_request(int64_t request_id, const String &operation, const Dictionary &result);
	void complete_leaderboard_request(int64_t request_id, const Dictionary &result);
	void complete_web_api_ticket_request(int64_t request_id, const Dictionary &result);
	void publish_workshop_items();

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	MxtSteamService();
	~MxtSteamService();

	bool initialize_steam();
	void shutdown_steam();
	bool is_compiled_with_steam() const;
	bool is_initialized() const { return initialized; }
	bool was_initialization_attempted() const { return init_attempted; }
	int64_t get_app_id() const { return static_cast<int64_t>(app_id); }
	int64_t get_steam_id() const { return static_cast<int64_t>(steam_id); }
	String get_persona_name() const { return persona_name; }
	String get_status_message() const { return status_message; }
	Dictionary get_status() const;

	int64_t create_workshop_item();
	int64_t submit_workshop_item_update(
			int64_t published_file_id,
			const String &title,
			const String &description,
			const String &content_path,
			const String &preview_path,
			const Array &tags,
			const String &metadata,
			const String &visibility,
			const String &change_note);
	bool refresh_subscribed_workshop_items();
	bool download_workshop_item(int64_t published_file_id, bool high_priority = true);
	bool open_workshop_item_page(int64_t published_file_id);
	Dictionary get_workshop_update_progress() const;
	Array get_workshop_items() const { return workshop_items.duplicate(true); }

	int64_t request_leaderboard_entries(
			const String &leaderboard_name,
			const String &request_type,
			int32_t range_start = 1,
			int32_t range_end = 100);
	int64_t request_web_api_auth_ticket(const String &identity);
	bool cancel_web_api_auth_ticket(int64_t ticket_handle);
};

} // namespace godot

#endif // MXT_STEAM_SERVICE_H
