#ifndef MXT_STEAM_SERVICE_H
#define MXT_STEAM_SERVICE_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstdint>

namespace godot {

class MxtSteamService : public Node {
	GDCLASS(MxtSteamService, Node)

private:
	String status_message;
	String persona_name;
	uint64_t steam_id = 0;
	uint32_t app_id = 0;
	bool initialized = false;
	bool init_attempted = false;

	void clear_account_state();
	void publish_status();
	void shutdown_steam_internal(bool publish_change);

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
};

} // namespace godot

#endif // MXT_STEAM_SERVICE_H
