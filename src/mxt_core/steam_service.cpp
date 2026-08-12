#include "mxt_core/steam_service.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#if defined(MXT_STEAMWORKS_ENABLED)
#include <steam/steam_api.h>
#endif

using namespace godot;

MxtSteamService::MxtSteamService()
{
	set_process(false);
}

MxtSteamService::~MxtSteamService()
{
	shutdown_steam_internal(false);
}

void MxtSteamService::_bind_methods()
{
	ClassDB::bind_method(D_METHOD("initialize_steam"), &MxtSteamService::initialize_steam);
	ClassDB::bind_method(D_METHOD("shutdown_steam"), &MxtSteamService::shutdown_steam);
	ClassDB::bind_method(D_METHOD("is_compiled_with_steam"), &MxtSteamService::is_compiled_with_steam);
	ClassDB::bind_method(D_METHOD("is_initialized"), &MxtSteamService::is_initialized);
	ClassDB::bind_method(D_METHOD("was_initialization_attempted"), &MxtSteamService::was_initialization_attempted);
	ClassDB::bind_method(D_METHOD("get_app_id"), &MxtSteamService::get_app_id);
	ClassDB::bind_method(D_METHOD("get_steam_id"), &MxtSteamService::get_steam_id);
	ClassDB::bind_method(D_METHOD("get_persona_name"), &MxtSteamService::get_persona_name);
	ClassDB::bind_method(D_METHOD("get_status_message"), &MxtSteamService::get_status_message);
	ClassDB::bind_method(D_METHOD("get_status"), &MxtSteamService::get_status);

	ADD_SIGNAL(MethodInfo("status_changed", PropertyInfo(Variant::DICTIONARY, "status")));
}

void MxtSteamService::_notification(int p_what)
{
	if (p_what == NOTIFICATION_READY) {
		initialize_steam();
	} else if (p_what == NOTIFICATION_PROCESS) {
#if defined(MXT_STEAMWORKS_ENABLED)
		if (initialized) {
			SteamAPI_RunCallbacks();
		}
#endif
	} else if (p_what == NOTIFICATION_EXIT_TREE) {
		shutdown_steam();
	}
}

void MxtSteamService::clear_account_state()
{
	persona_name = String();
	steam_id = 0;
	app_id = 0;
}

void MxtSteamService::publish_status()
{
	emit_signal("status_changed", get_status());
}

bool MxtSteamService::initialize_steam()
{
	if (initialized) {
		return true;
	}

	init_attempted = true;
	clear_account_state();

#if defined(MXT_STEAMWORKS_ENABLED)
	SteamErrMsg error_message = {};
	const ESteamAPIInitResult result = SteamAPI_InitEx(&error_message);
	if (result != k_ESteamAPIInitResult_OK) {
		status_message = String::utf8(error_message);
		if (status_message.is_empty()) {
			status_message = "Steam initialization failed.";
		}
		set_process(false);
		UtilityFunctions::print("MXT Steam: ", status_message);
		publish_status();
		return false;
	}

	ISteamUtils* utils = SteamUtils();
	ISteamUser* user = SteamUser();
	ISteamFriends* friends = SteamFriends();
	if (!utils || !user || !friends) {
		SteamAPI_Shutdown();
		status_message = "Steam initialized without the required client interfaces.";
		set_process(false);
		UtilityFunctions::print("MXT Steam: ", status_message);
		publish_status();
		return false;
	}

	app_id = static_cast<uint32_t>(utils->GetAppID());
	steam_id = user->GetSteamID().ConvertToUint64();
	persona_name = String::utf8(friends->GetPersonaName());
	initialized = true;
	status_message = "Steam initialized.";
	set_process(true);
	UtilityFunctions::print(
			"MXT Steam: initialized app_id=", app_id,
			" steam_id=", static_cast<int64_t>(steam_id),
			" persona=", persona_name);
	publish_status();
	return true;
#else
	status_message = "Steamworks support was not compiled into this build.";
	set_process(false);
	UtilityFunctions::print("MXT Steam: ", status_message);
	publish_status();
	return false;
#endif
}

void MxtSteamService::shutdown_steam()
{
	shutdown_steam_internal(true);
}

void MxtSteamService::shutdown_steam_internal(bool publish_change)
{
	if (!initialized) {
		return;
	}

#if defined(MXT_STEAMWORKS_ENABLED)
	SteamAPI_Shutdown();
#endif

	initialized = false;
	set_process(false);
	clear_account_state();
	status_message = "Steam shut down.";
	if (publish_change) {
		publish_status();
	}
}

bool MxtSteamService::is_compiled_with_steam() const
{
#if defined(MXT_STEAMWORKS_ENABLED)
	return true;
#else
	return false;
#endif
}

Dictionary MxtSteamService::get_status() const
{
	Dictionary status;
	status["compiled_with_steam"] = is_compiled_with_steam();
	status["initialization_attempted"] = init_attempted;
	status["initialized"] = initialized;
	status["app_id"] = static_cast<int64_t>(app_id);
	status["steam_id"] = static_cast<int64_t>(steam_id);
	status["persona_name"] = persona_name;
	status["message"] = status_message;
	return status;
}
