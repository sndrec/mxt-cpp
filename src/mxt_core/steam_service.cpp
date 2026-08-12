#include "mxt_core/steam_service.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <array>
#include <vector>

#if defined(MXT_STEAMWORKS_ENABLED)
#include <steam/steam_api.h>
#endif

using namespace godot;

namespace {

static Dictionary workshop_result(bool success, const String &message)
{
	Dictionary result;
	result["success"] = success;
	result["message"] = message;
	return result;
}

static String absolute_path(const String &path)
{
	if (path.begins_with("res://") || path.begins_with("user://")) {
		return ProjectSettings::get_singleton()->globalize_path(path);
	}
	return path;
}

#if defined(MXT_STEAMWORKS_ENABLED)

static String steam_result_name(EResult result)
{
	switch (result) {
		case k_EResultOK: return "OK";
		case k_EResultFail: return "generic failure";
		case k_EResultNoConnection: return "no Steam connection";
		case k_EResultAccessDenied: return "access denied";
		case k_EResultTimeout: return "request timed out";
		case k_EResultNotLoggedOn: return "not logged on";
		case k_EResultInsufficientPrivilege: return "insufficient privilege";
		case k_EResultLimitExceeded: return "limit exceeded";
		case k_EResultFileNotFound: return "file not found";
		case k_EResultDuplicateRequest: return "duplicate request";
		case k_EResultServiceUnavailable: return "Steam service unavailable";
		default: return String("Steam error ") + String::num_int64(static_cast<int64_t>(result));
	}
}

static String update_status_name(EItemUpdateStatus status)
{
	switch (status) {
		case k_EItemUpdateStatusPreparingConfig: return "preparing_config";
		case k_EItemUpdateStatusPreparingContent: return "preparing_content";
		case k_EItemUpdateStatusUploadingContent: return "uploading_content";
		case k_EItemUpdateStatusUploadingPreviewFile: return "uploading_preview";
		case k_EItemUpdateStatusCommittingChanges: return "committing";
		case k_EItemUpdateStatusInvalid: return "idle";
	}
	return "unknown";
}

static Dictionary workshop_item_dictionary(ISteamUGC *ugc, PublishedFileId_t item_id)
{
	Dictionary item;
	item["published_file_id"] = static_cast<int64_t>(item_id);
	const uint32 state = ugc->GetItemState(item_id);
	item["subscribed"] = (state & k_EItemStateSubscribed) != 0;
	item["legacy"] = (state & k_EItemStateLegacyItem) != 0;
	item["installed"] = (state & k_EItemStateInstalled) != 0;
	item["needs_update"] = (state & k_EItemStateNeedsUpdate) != 0;
	item["downloading"] = (state & k_EItemStateDownloading) != 0;
	item["download_pending"] = (state & k_EItemStateDownloadPending) != 0;
	item["locally_disabled"] = (state & k_EItemStateDisabledLocally) != 0;
	uint64 downloaded = 0;
	uint64 total = 0;
	if (ugc->GetItemDownloadInfo(item_id, &downloaded, &total)) {
		item["downloaded_bytes"] = static_cast<int64_t>(downloaded);
		item["download_total_bytes"] = static_cast<int64_t>(total);
	}
	std::array<char, 4096> folder = {};
	uint64 size_on_disk = 0;
	uint32 timestamp = 0;
	if ((state & k_EItemStateInstalled) != 0 &&
			ugc->GetItemInstallInfo(item_id, &size_on_disk, folder.data(), static_cast<uint32>(folder.size()), &timestamp)) {
		item["install_path"] = String::utf8(folder.data());
		item["size_on_disk"] = static_cast<int64_t>(size_on_disk);
		item["install_timestamp"] = static_cast<int64_t>(timestamp);
	}
	if ((state & k_EItemStateDisabledLocally) != 0) {
		item["status"] = "disabled";
	} else if ((state & (k_EItemStateDownloading | k_EItemStateDownloadPending)) != 0) {
		item["status"] = "downloading";
	} else if ((state & k_EItemStateNeedsUpdate) != 0) {
		item["status"] = "needs_update";
	} else if ((state & k_EItemStateInstalled) != 0) {
		item["status"] = "installed";
	} else {
		item["status"] = "missing";
	}
	return item;
}

#endif

} // namespace

namespace godot {

#if defined(MXT_STEAMWORKS_ENABLED)

struct SteamWorkshopState {
	MxtSteamService *owner = nullptr;
	CCallResult<SteamWorkshopState, CreateItemResult_t> create_call;
	CCallResult<SteamWorkshopState, SubmitItemUpdateResult_t> submit_call;
	CCallback<SteamWorkshopState, DownloadItemResult_t> download_callback;
	CCallback<SteamWorkshopState, ItemInstalled_t> installed_callback;
	CCallback<SteamWorkshopState, RemoteStoragePublishedFileSubscribed_t> subscribed_callback;
	CCallback<SteamWorkshopState, RemoteStoragePublishedFileUnsubscribed_t> unsubscribed_callback;
	int64_t create_request_id = 0;
	int64_t submit_request_id = 0;
	UGCUpdateHandle_t update_handle = k_UGCUpdateHandleInvalid;

	explicit SteamWorkshopState(MxtSteamService *p_owner) :
			owner(p_owner),
			download_callback(this, &SteamWorkshopState::on_download_result),
			installed_callback(this, &SteamWorkshopState::on_item_installed),
			subscribed_callback(this, &SteamWorkshopState::on_subscribed),
			unsubscribed_callback(this, &SteamWorkshopState::on_unsubscribed)
	{
	}

	void on_create_item(CreateItemResult_t *value, bool io_failure)
	{
		Dictionary result = workshop_result(!io_failure && value->m_eResult == k_EResultOK,
				io_failure ? String("Steam I/O failure") : steam_result_name(value->m_eResult));
		result["published_file_id"] = static_cast<int64_t>(value->m_nPublishedFileId);
		result["legal_agreement_required"] = value->m_bUserNeedsToAcceptWorkshopLegalAgreement;
		const int64_t request_id = create_request_id;
		create_request_id = 0;
		owner->complete_workshop_request(request_id, "create_item", result);
	}

	void on_submit_item(SubmitItemUpdateResult_t *value, bool io_failure)
	{
		Dictionary result = workshop_result(!io_failure && value->m_eResult == k_EResultOK,
				io_failure ? String("Steam I/O failure") : steam_result_name(value->m_eResult));
		result["published_file_id"] = static_cast<int64_t>(value->m_nPublishedFileId);
		result["legal_agreement_required"] = value->m_bUserNeedsToAcceptWorkshopLegalAgreement;
		const int64_t request_id = submit_request_id;
		submit_request_id = 0;
		update_handle = k_UGCUpdateHandleInvalid;
		owner->complete_workshop_request(request_id, "submit_update", result);
	}

	void on_download_result(DownloadItemResult_t *value)
	{
		if (value->m_unAppID != owner->get_app_id()) return;
		Dictionary result = workshop_result(value->m_eResult == k_EResultOK, steam_result_name(value->m_eResult));
		result["published_file_id"] = static_cast<int64_t>(value->m_nPublishedFileId);
		owner->complete_workshop_request(0, "download", result);
		owner->refresh_subscribed_workshop_items();
	}

	void on_item_installed(ItemInstalled_t *value)
	{
		if (value->m_unAppID == owner->get_app_id()) owner->refresh_subscribed_workshop_items();
	}

	void on_subscribed(RemoteStoragePublishedFileSubscribed_t *value)
	{
		if (value->m_nAppID == owner->get_app_id()) owner->refresh_subscribed_workshop_items();
	}

	void on_unsubscribed(RemoteStoragePublishedFileUnsubscribed_t *value)
	{
		if (value->m_nAppID == owner->get_app_id()) owner->refresh_subscribed_workshop_items();
	}
};

#else

struct SteamWorkshopState {};

#endif

} // namespace godot

MxtSteamService::MxtSteamService()
{
#if defined(MXT_STEAMWORKS_ENABLED)
	workshop_state = new SteamWorkshopState(this);
#endif
	set_process(false);
}

MxtSteamService::~MxtSteamService()
{
	delete workshop_state;
	workshop_state = nullptr;
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
	ClassDB::bind_method(D_METHOD("create_workshop_item"), &MxtSteamService::create_workshop_item);
	ClassDB::bind_method(
			D_METHOD("submit_workshop_item_update", "published_file_id", "title", "description", "content_path", "preview_path", "tags", "metadata", "visibility", "change_note"),
			&MxtSteamService::submit_workshop_item_update);
	ClassDB::bind_method(D_METHOD("refresh_subscribed_workshop_items"), &MxtSteamService::refresh_subscribed_workshop_items);
	ClassDB::bind_method(D_METHOD("download_workshop_item", "published_file_id", "high_priority"), &MxtSteamService::download_workshop_item, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("open_workshop_item_page", "published_file_id"), &MxtSteamService::open_workshop_item_page);
	ClassDB::bind_method(D_METHOD("get_workshop_update_progress"), &MxtSteamService::get_workshop_update_progress);
	ClassDB::bind_method(D_METHOD("get_workshop_items"), &MxtSteamService::get_workshop_items);

	ADD_SIGNAL(MethodInfo("status_changed", PropertyInfo(Variant::DICTIONARY, "status")));
	ADD_SIGNAL(MethodInfo(
			"workshop_request_completed",
			PropertyInfo(Variant::INT, "request_id"),
			PropertyInfo(Variant::STRING, "operation"),
			PropertyInfo(Variant::DICTIONARY, "result")));
	ADD_SIGNAL(MethodInfo("workshop_items_changed", PropertyInfo(Variant::ARRAY, "items")));
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
	ISteamUGC *ugc = SteamUGC();
	if (!utils || !user || !friends || !ugc) {
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
	refresh_subscribed_workshop_items();
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
	if (workshop_state) {
		workshop_state->create_call.Cancel();
		workshop_state->submit_call.Cancel();
		workshop_state->create_request_id = 0;
		workshop_state->submit_request_id = 0;
		workshop_state->update_handle = k_UGCUpdateHandleInvalid;
	}
	SteamAPI_Shutdown();
#endif

	initialized = false;
	set_process(false);
	clear_account_state();
	workshop_items.clear();
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

int64_t MxtSteamService::allocate_workshop_request_id()
{
	const int64_t request_id = next_workshop_request_id;
	if (next_workshop_request_id == INT64_MAX) {
		next_workshop_request_id = 1;
	} else {
		++next_workshop_request_id;
	}
	return request_id;
}

void MxtSteamService::complete_workshop_request(
		int64_t request_id,
		const String &operation,
		const Dictionary &result)
{
	emit_signal("workshop_request_completed", request_id, operation, result);
}

void MxtSteamService::publish_workshop_items()
{
	emit_signal("workshop_items_changed", workshop_items.duplicate(true));
}

int64_t MxtSteamService::create_workshop_item()
{
	const int64_t request_id = allocate_workshop_request_id();
#if defined(MXT_STEAMWORKS_ENABLED)
	if (!initialized || !workshop_state || !SteamUGC()) {
		complete_workshop_request(request_id, "create_item", workshop_result(false, "Steam Workshop is unavailable."));
		return request_id;
	}
	if (workshop_state->create_request_id != 0 || workshop_state->submit_request_id != 0) {
		complete_workshop_request(request_id, "create_item", workshop_result(false, "Another Workshop publishing operation is active."));
		return request_id;
	}
	const SteamAPICall_t call = SteamUGC()->CreateItem(static_cast<AppId_t>(app_id), k_EWorkshopFileTypeCommunity);
	if (call == k_uAPICallInvalid) {
		complete_workshop_request(request_id, "create_item", workshop_result(false, "Steam rejected the Workshop create request."));
		return request_id;
	}
	workshop_state->create_request_id = request_id;
	workshop_state->create_call.Set(call, workshop_state, &SteamWorkshopState::on_create_item);
#else
	complete_workshop_request(request_id, "create_item", workshop_result(false, "Steamworks support was not compiled into this build."));
#endif
	return request_id;
}

int64_t MxtSteamService::submit_workshop_item_update(
		int64_t published_file_id,
		const String &title,
		const String &description,
		const String &content_path,
		const String &preview_path,
		const Array &tags,
		const String &metadata,
		const String &visibility,
		const String &change_note)
{
	const int64_t request_id = allocate_workshop_request_id();
#if defined(MXT_STEAMWORKS_ENABLED)
	if (!initialized || !workshop_state || !SteamUGC()) {
		complete_workshop_request(request_id, "submit_update", workshop_result(false, "Steam Workshop is unavailable."));
		return request_id;
	}
	if (published_file_id <= 0 || title.is_empty() || title.length() > 128 || description.length() > 8000 ||
			metadata.length() >= k_cchDeveloperMetadataMax || tags.is_empty() || tags.size() > 16) {
		complete_workshop_request(request_id, "submit_update", workshop_result(false, "Workshop metadata is missing or exceeds its supported limits."));
		return request_id;
	}
	if (workshop_state->create_request_id != 0 || workshop_state->submit_request_id != 0) {
		complete_workshop_request(request_id, "submit_update", workshop_result(false, "Another Workshop publishing operation is active."));
		return request_id;
	}
	const String use_content_path = absolute_path(content_path);
	const String use_preview_path = absolute_path(preview_path);
	if (!DirAccess::open(use_content_path).is_valid() || !FileAccess::file_exists(use_preview_path)) {
		complete_workshop_request(request_id, "submit_update", workshop_result(false, "Workshop content or preview path does not exist."));
		return request_id;
	}
	ERemoteStoragePublishedFileVisibility steam_visibility = k_ERemoteStoragePublishedFileVisibilityPublic;
	if (visibility == "friends_only") {
		steam_visibility = k_ERemoteStoragePublishedFileVisibilityFriendsOnly;
	} else if (visibility == "private") {
		steam_visibility = k_ERemoteStoragePublishedFileVisibilityPrivate;
	} else if (visibility == "unlisted") {
		steam_visibility = k_ERemoteStoragePublishedFileVisibilityUnlisted;
	} else if (visibility != "public") {
		complete_workshop_request(request_id, "submit_update", workshop_result(false, "Workshop visibility is invalid."));
		return request_id;
	}
	for (int64_t i = 0; i < tags.size(); ++i) {
		const String tag = tags[i];
		if (tag.is_empty() || tag.length() > 64) {
			complete_workshop_request(request_id, "submit_update", workshop_result(false, "Workshop tags must contain 1 to 64 characters."));
			return request_id;
		}
	}
	const UGCUpdateHandle_t handle = SteamUGC()->StartItemUpdate(
			static_cast<AppId_t>(app_id),
			static_cast<PublishedFileId_t>(published_file_id));
	if (handle == k_UGCUpdateHandleInvalid) {
		complete_workshop_request(request_id, "submit_update", workshop_result(false, "Steam could not start the Workshop update."));
		return request_id;
	}
	std::vector<CharString> encoded_tags;
	std::vector<const char *> tag_pointers;
	encoded_tags.reserve(tags.size());
	tag_pointers.reserve(tags.size());
	for (int64_t i = 0; i < tags.size(); ++i) {
		const String tag = tags[i];
		encoded_tags.push_back(tag.utf8());
	}
	for (const CharString &tag : encoded_tags) tag_pointers.push_back(tag.get_data());
	SteamParamStringArray_t steam_tags = { tag_pointers.data(), static_cast<int32>(tag_pointers.size()) };
	const CharString title_utf8 = title.utf8();
	const CharString description_utf8 = description.utf8();
	const CharString metadata_utf8 = metadata.utf8();
	const CharString content_utf8 = use_content_path.utf8();
	const CharString preview_utf8 = use_preview_path.utf8();
	const bool configured =
			SteamUGC()->SetItemTitle(handle, title_utf8.get_data()) &&
			SteamUGC()->SetItemDescription(handle, description_utf8.get_data()) &&
			SteamUGC()->SetItemMetadata(handle, metadata_utf8.get_data()) &&
			SteamUGC()->SetItemVisibility(handle, steam_visibility) &&
			SteamUGC()->SetItemTags(handle, &steam_tags) &&
			SteamUGC()->SetItemContent(handle, content_utf8.get_data()) &&
			SteamUGC()->SetItemPreview(handle, preview_utf8.get_data());
	if (!configured) {
		complete_workshop_request(request_id, "submit_update", workshop_result(false, "Steam rejected one or more Workshop update fields."));
		return request_id;
	}
	const CharString note_utf8 = change_note.utf8();
	const SteamAPICall_t call = SteamUGC()->SubmitItemUpdate(handle, note_utf8.get_data());
	if (call == k_uAPICallInvalid) {
		complete_workshop_request(request_id, "submit_update", workshop_result(false, "Steam rejected the Workshop submit request."));
		return request_id;
	}
	workshop_state->submit_request_id = request_id;
	workshop_state->update_handle = handle;
	workshop_state->submit_call.Set(call, workshop_state, &SteamWorkshopState::on_submit_item);
#else
	complete_workshop_request(request_id, "submit_update", workshop_result(false, "Steamworks support was not compiled into this build."));
#endif
	return request_id;
}

bool MxtSteamService::refresh_subscribed_workshop_items()
{
#if defined(MXT_STEAMWORKS_ENABLED)
	if (!initialized || !SteamUGC()) return false;
	ISteamUGC *ugc = SteamUGC();
	const uint32 count = ugc->GetNumSubscribedItems(true);
	std::vector<PublishedFileId_t> ids(count);
	const uint32 received = count == 0 ? 0 : ugc->GetSubscribedItems(ids.data(), count, true);
	workshop_items.clear();
	for (uint32 i = 0; i < received; ++i) {
		Dictionary item = workshop_item_dictionary(ugc, ids[i]);
		workshop_items.push_back(item);
		const uint32 state = ugc->GetItemState(ids[i]);
		if ((state & k_EItemStateDisabledLocally) == 0 &&
				(state & (k_EItemStateInstalled | k_EItemStateDownloading | k_EItemStateDownloadPending)) == 0) {
			ugc->DownloadItem(ids[i], false);
		} else if ((state & k_EItemStateNeedsUpdate) != 0 &&
				(state & (k_EItemStateDownloading | k_EItemStateDownloadPending)) == 0) {
			ugc->DownloadItem(ids[i], false);
		}
	}
	publish_workshop_items();
	return true;
#else
	return false;
#endif
}

bool MxtSteamService::download_workshop_item(int64_t published_file_id, bool high_priority)
{
#if defined(MXT_STEAMWORKS_ENABLED)
	return initialized && published_file_id > 0 && SteamUGC() &&
			SteamUGC()->DownloadItem(static_cast<PublishedFileId_t>(published_file_id), high_priority);
#else
	return false;
#endif
}

bool MxtSteamService::open_workshop_item_page(int64_t published_file_id)
{
#if defined(MXT_STEAMWORKS_ENABLED)
	if (!initialized || published_file_id <= 0 || !SteamFriends()) return false;
	const String url = String("https://steamcommunity.com/sharedfiles/filedetails/?id=") + String::num_int64(published_file_id);
	const CharString url_utf8 = url.utf8();
	SteamFriends()->ActivateGameOverlayToWebPage(url_utf8.get_data());
	return true;
#else
	return false;
#endif
}

Dictionary MxtSteamService::get_workshop_update_progress() const
{
	Dictionary progress;
	progress["active"] = false;
	progress["status"] = "idle";
	progress["processed_bytes"] = 0;
	progress["total_bytes"] = 0;
#if defined(MXT_STEAMWORKS_ENABLED)
	if (!initialized || !workshop_state || workshop_state->update_handle == k_UGCUpdateHandleInvalid || !SteamUGC()) {
		return progress;
	}
	uint64 processed = 0;
	uint64 total = 0;
	const EItemUpdateStatus status = SteamUGC()->GetItemUpdateProgress(workshop_state->update_handle, &processed, &total);
	progress["active"] = status != k_EItemUpdateStatusInvalid;
	progress["status"] = update_status_name(status);
	progress["processed_bytes"] = static_cast<int64_t>(processed);
	progress["total_bytes"] = static_cast<int64_t>(total);
#endif
	return progress;
}
