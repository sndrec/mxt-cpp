#include "platform/steam/steam_service.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/config_file.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/time.hpp>
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

static constexpr uint64_t WORKSHOP_PREVIEW_MAX_BYTES = 1'000'000;
static constexpr const char *TRACKED_WORKSHOP_ITEMS_PATH = "user://tracked_workshop_items.cfg";

static Dictionary request_result(bool success, const String &message)
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
	item["item_state_bits"] = static_cast<int64_t>(state);
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

static void workshop_trace(const String &event, const Dictionary &fields)
{
	UtilityFunctions::print(
			"MXT_WORKSHOP_NATIVE ticks_msec=",
			static_cast<int64_t>(Time::get_singleton()->get_ticks_msec()),
			" event=", event,
			" fields=", fields);
}

#endif

} // namespace

namespace godot {

#if defined(MXT_STEAMWORKS_ENABLED)

struct SteamWorkshopState {
	struct LeaderboardReadRequest {
		int64_t request_id = 0;
		String leaderboard_name;
		ELeaderboardDataRequest data_request = k_ELeaderboardDataRequestGlobal;
		int32 range_start = 1;
		int32 range_end = 100;
	};

	static constexpr uint32 LEADERBOARD_QUEUE_CAPACITY = 16;

	MxtSteamService *owner = nullptr;
	CCallResult<SteamWorkshopState, CreateItemResult_t> create_call;
	CCallResult<SteamWorkshopState, SubmitItemUpdateResult_t> submit_call;
	CCallback<SteamWorkshopState, DownloadItemResult_t> download_callback;
	CCallback<SteamWorkshopState, ItemInstalled_t> installed_callback;
	CCallback<SteamWorkshopState, RemoteStoragePublishedFileSubscribed_t> subscribed_callback;
	CCallback<SteamWorkshopState, RemoteStoragePublishedFileUnsubscribed_t> unsubscribed_callback;
	CCallResult<SteamWorkshopState, LeaderboardFindResult_t> leaderboard_find_call;
	CCallResult<SteamWorkshopState, LeaderboardScoresDownloaded_t> leaderboard_download_call;
	CCallResult<SteamWorkshopState, RemoteStorageFileShareResult_t> replay_share_call;
	CCallResult<SteamWorkshopState, LeaderboardFindResult_t> replay_leaderboard_find_call;
	CCallResult<SteamWorkshopState, LeaderboardScoresDownloaded_t> replay_entry_download_call;
	CCallResult<SteamWorkshopState, LeaderboardUGCSet_t> replay_attach_call;
	CCallResult<SteamWorkshopState, RemoteStorageDownloadUGCResult_t> replay_download_call;
	CCallback<SteamWorkshopState, GetTicketForWebApiResponse_t> web_api_ticket_callback;
	int64_t create_request_id = 0;
	int64_t submit_request_id = 0;
	UGCUpdateHandle_t update_handle = k_UGCUpdateHandleInvalid;
	std::array<LeaderboardReadRequest, LEADERBOARD_QUEUE_CAPACITY> leaderboard_queue;
	uint32 leaderboard_queue_head = 0;
	uint32 leaderboard_queue_count = 0;
	LeaderboardReadRequest active_leaderboard_request;
	bool leaderboard_request_active = false;
	int64_t web_api_ticket_request_id = 0;
	HAuthTicket web_api_ticket_handle = k_HAuthTicketInvalid;
	String web_api_ticket_identity;
	int64_t replay_upload_request_id = 0;
	String replay_upload_leaderboard_name;
	String replay_upload_remote_filename;
	uint32 replay_upload_digest_words[8] = {};
	UGCHandle_t replay_upload_handle = k_UGCHandleInvalid;
	bool replay_attach_existing = false;
	int64_t replay_download_request_id = 0;
	UGCHandle_t replay_download_handle = k_UGCHandleInvalid;
	int64_t replay_download_maximum_bytes = 0;

	explicit SteamWorkshopState(MxtSteamService *p_owner) :
			owner(p_owner),
			download_callback(this, &SteamWorkshopState::on_download_result),
			installed_callback(this, &SteamWorkshopState::on_item_installed),
			subscribed_callback(this, &SteamWorkshopState::on_subscribed),
			unsubscribed_callback(this, &SteamWorkshopState::on_unsubscribed),
			web_api_ticket_callback(this, &SteamWorkshopState::on_web_api_ticket)
	{
	}

	void start_next_leaderboard_request()
	{
		if (leaderboard_request_active || leaderboard_queue_count == 0 || !SteamUserStats()) return;
		active_leaderboard_request = leaderboard_queue[leaderboard_queue_head];
		leaderboard_queue_head = (leaderboard_queue_head + 1) % LEADERBOARD_QUEUE_CAPACITY;
		--leaderboard_queue_count;
		leaderboard_request_active = true;
		const CharString name_utf8 = active_leaderboard_request.leaderboard_name.utf8();
		const SteamAPICall_t call = SteamUserStats()->FindLeaderboard(name_utf8.get_data());
		if (call == k_uAPICallInvalid) {
			finish_leaderboard_request(request_result(false, "Steam rejected the leaderboard lookup."));
			return;
		}
		leaderboard_find_call.Set(call, this, &SteamWorkshopState::on_leaderboard_found);
	}

	bool enqueue_leaderboard_request(const LeaderboardReadRequest &request)
	{
		if (leaderboard_queue_count >= LEADERBOARD_QUEUE_CAPACITY) return false;
		const uint32 tail = (leaderboard_queue_head + leaderboard_queue_count) % LEADERBOARD_QUEUE_CAPACITY;
		leaderboard_queue[tail] = request;
		++leaderboard_queue_count;
		start_next_leaderboard_request();
		return true;
	}

	void finish_leaderboard_request(const Dictionary &result)
	{
		const int64_t request_id = active_leaderboard_request.request_id;
		active_leaderboard_request = LeaderboardReadRequest();
		leaderboard_request_active = false;
		owner->complete_leaderboard_request(request_id, result);
		start_next_leaderboard_request();
	}

	void on_leaderboard_found(LeaderboardFindResult_t *value, bool io_failure)
	{
		if (io_failure || !value->m_bLeaderboardFound || value->m_hSteamLeaderboard == 0 || !SteamUserStats()) {
			finish_leaderboard_request(request_result(false,
					io_failure ? String("Steam I/O failure") : String("Leaderboard was not found.")));
			return;
		}
		const SteamAPICall_t call = SteamUserStats()->DownloadLeaderboardEntries(
				value->m_hSteamLeaderboard,
				active_leaderboard_request.data_request,
				active_leaderboard_request.range_start,
				active_leaderboard_request.range_end);
		if (call == k_uAPICallInvalid) {
			finish_leaderboard_request(request_result(false, "Steam rejected the leaderboard download."));
			return;
		}
		leaderboard_download_call.Set(call, this, &SteamWorkshopState::on_leaderboard_downloaded);
	}

	void on_leaderboard_downloaded(LeaderboardScoresDownloaded_t *value, bool io_failure)
	{
		if (io_failure || !SteamUserStats()) {
			finish_leaderboard_request(request_result(false, "Steam I/O failure"));
			return;
		}
		Array entries;
		for (int32 i = 0; i < value->m_cEntryCount; ++i) {
			LeaderboardEntry_t steam_entry = {};
			std::array<int32, k_cLeaderboardDetailsMax> details = {};
			if (!SteamUserStats()->GetDownloadedLeaderboardEntry(
						value->m_hSteamLeaderboardEntries,
						i,
						&steam_entry,
						details.data(),
						static_cast<int32>(details.size()))) {
				continue;
			}
			Dictionary entry;
			entry["steam_id"] = static_cast<int64_t>(steam_entry.m_steamIDUser.ConvertToUint64());
			entry["global_rank"] = steam_entry.m_nGlobalRank;
			entry["score"] = steam_entry.m_nScore;
			entry["ugc_handle"] = steam_entry.m_hUGC == k_UGCHandleInvalid ?
					static_cast<int64_t>(0) : static_cast<int64_t>(steam_entry.m_hUGC);
			if (SteamFriends()) {
				entry["persona_name"] = String::utf8(SteamFriends()->GetFriendPersonaName(steam_entry.m_steamIDUser));
			}
			Array entry_details;
			const int32 detail_count = steam_entry.m_cDetails < static_cast<int32>(details.size()) ?
					steam_entry.m_cDetails : static_cast<int32>(details.size());
			for (int32 detail_index = 0; detail_index < detail_count; ++detail_index) {
				entry_details.push_back(details[detail_index]);
			}
			entry["details"] = entry_details;
			entries.push_back(entry);
		}
		Dictionary result = request_result(true, "Leaderboard entries downloaded.");
		result["leaderboard_name"] = active_leaderboard_request.leaderboard_name;
		result["entries"] = entries;
		finish_leaderboard_request(result);
	}

	void on_web_api_ticket(GetTicketForWebApiResponse_t *value)
	{
		if (web_api_ticket_request_id == 0 || value->m_hAuthTicket != web_api_ticket_handle) return;
		Dictionary result = request_result(value->m_eResult == k_EResultOK,
				value->m_eResult == k_EResultOK ? String("Web API ticket created.") : steam_result_name(value->m_eResult));
		result["identity"] = web_api_ticket_identity;
		result["ticket_handle"] = static_cast<int64_t>(value->m_hAuthTicket);
		if (value->m_eResult == k_EResultOK && value->m_cubTicket > 0 &&
				value->m_cubTicket <= GetTicketForWebApiResponse_t::k_nCubTicketMaxLength) {
			static constexpr char HEX[] = "0123456789abcdef";
			std::array<char, GetTicketForWebApiResponse_t::k_nCubTicketMaxLength * 2 + 1> encoded = {};
			for (int i = 0; i < value->m_cubTicket; ++i) {
				encoded[static_cast<size_t>(i) * 2] = HEX[value->m_rgubTicket[i] >> 4];
				encoded[static_cast<size_t>(i) * 2 + 1] = HEX[value->m_rgubTicket[i] & 0x0f];
			}
			result["ticket_hex"] = String::utf8(encoded.data(), value->m_cubTicket * 2);
		} else {
			result["success"] = false;
			if (SteamUser() && web_api_ticket_handle != k_HAuthTicketInvalid) {
				SteamUser()->CancelAuthTicket(web_api_ticket_handle);
			}
		}
		const int64_t request_id = web_api_ticket_request_id;
		web_api_ticket_request_id = 0;
		web_api_ticket_handle = k_HAuthTicketInvalid;
		web_api_ticket_identity = String();
		owner->complete_web_api_ticket_request(request_id, result);
	}

	void finish_replay_upload(const Dictionary &result)
	{
		const int64_t request_id = replay_upload_request_id;
		replay_upload_request_id = 0;
		replay_upload_leaderboard_name = String();
		replay_upload_remote_filename = String();
		for (uint32 &word : replay_upload_digest_words) word = 0;
		replay_upload_handle = k_UGCHandleInvalid;
		replay_attach_existing = false;
		owner->complete_leaderboard_replay_upload(request_id, result);
	}

	void on_replay_file_shared(RemoteStorageFileShareResult_t *value, bool io_failure)
	{
		if (replay_upload_request_id == 0) return;
		if (io_failure || value->m_eResult != k_EResultOK || value->m_hFile == k_UGCHandleInvalid || !SteamUserStats()) {
			finish_replay_upload(request_result(false, io_failure ? String("Steam replay share I/O failure") : steam_result_name(value->m_eResult)));
			return;
		}
		replay_upload_handle = value->m_hFile;
		const CharString name_utf8 = replay_upload_leaderboard_name.utf8();
		const SteamAPICall_t call = SteamUserStats()->FindLeaderboard(name_utf8.get_data());
		if (call == k_uAPICallInvalid) {
			finish_replay_upload(request_result(false, "Steam rejected the replay leaderboard lookup."));
			return;
		}
		replay_leaderboard_find_call.Set(call, this, &SteamWorkshopState::on_replay_leaderboard_found);
	}

	void on_replay_leaderboard_found(LeaderboardFindResult_t *value, bool io_failure)
	{
		if (replay_upload_request_id == 0) return;
		if (io_failure || !value->m_bLeaderboardFound || value->m_hSteamLeaderboard == 0 || !SteamUserStats() || !SteamUser()) {
			finish_replay_upload(request_result(false, io_failure ? String("Steam replay leaderboard I/O failure") : String("Replay leaderboard was not found.")));
			return;
		}
		if (replay_attach_existing) {
			const SteamAPICall_t attach_call = SteamUserStats()->AttachLeaderboardUGC(
					value->m_hSteamLeaderboard, replay_upload_handle);
			if (attach_call == k_uAPICallInvalid) {
				finish_replay_upload(request_result(false, "Steam rejected the existing replay attachment."));
				return;
			}
			replay_attach_call.Set(attach_call, this, &SteamWorkshopState::on_replay_attached);
			return;
		}
		CSteamID user = SteamUser()->GetSteamID();
		const SteamAPICall_t call = SteamUserStats()->DownloadLeaderboardEntriesForUsers(value->m_hSteamLeaderboard, &user, 1);
		if (call == k_uAPICallInvalid) {
			finish_replay_upload(request_result(false, "Steam rejected the retained-score replay check."));
			return;
		}
		replay_entry_download_call.Set(call, this, &SteamWorkshopState::on_replay_entry_downloaded);
	}

	void on_replay_entry_downloaded(LeaderboardScoresDownloaded_t *value, bool io_failure)
	{
		if (replay_upload_request_id == 0) return;
		static constexpr int32 LEADERBOARD_DETAILS_MAGIC = 0x3154584D;
		static constexpr int32 PREVIOUS_DETAILS_REVISION = 2;
		static constexpr int32 PREVIOUS_DETAILS_WORD_COUNT = 29;
		static constexpr int32 CURRENT_DETAILS_REVISION = 3;
		static constexpr int32 CURRENT_DETAILS_WORD_COUNT = 30;
		LeaderboardEntry_t entry = {};
		std::array<int32, CURRENT_DETAILS_WORD_COUNT> details = {};
		bool digest_matches = false;
		if (!io_failure && value->m_cEntryCount == 1 && SteamUserStats() &&
				SteamUserStats()->GetDownloadedLeaderboardEntry(
						value->m_hSteamLeaderboardEntries, 0, &entry, details.data(), details.size()) &&
				static_cast<uint32>(details[0]) == static_cast<uint32>(LEADERBOARD_DETAILS_MAGIC) &&
				((details[1] == PREVIOUS_DETAILS_REVISION && entry.m_cDetails >= PREVIOUS_DETAILS_WORD_COUNT) ||
						(details[1] == CURRENT_DETAILS_REVISION && entry.m_cDetails >= CURRENT_DETAILS_WORD_COUNT))) {
			digest_matches = true;
			for (int index = 0; index < 8; ++index) {
				if (static_cast<uint32>(details[5 + index]) != replay_upload_digest_words[index]) {
					digest_matches = false;
					break;
				}
			}
		}
		if (!digest_matches || !SteamUserStats()) {
			if (SteamRemoteStorage()) {
				const CharString filename_utf8 = replay_upload_remote_filename.utf8();
				SteamRemoteStorage()->FileDelete(filename_utf8.get_data());
			}
			Dictionary result = request_result(false, io_failure ? String("Steam retained-score replay check failed.") : String("The retained score changed before its replay could be attached."));
			result["retryable"] = io_failure;
			finish_replay_upload(result);
			return;
		}
		const SteamAPICall_t call = SteamUserStats()->AttachLeaderboardUGC(value->m_hSteamLeaderboard, replay_upload_handle);
		if (call == k_uAPICallInvalid) {
			finish_replay_upload(request_result(false, "Steam rejected the replay attachment."));
			return;
		}
		replay_attach_call.Set(call, this, &SteamWorkshopState::on_replay_attached);
	}

	void on_replay_attached(LeaderboardUGCSet_t *value, bool io_failure)
	{
		if (replay_upload_request_id == 0) return;
		Dictionary result = request_result(!io_failure && value->m_eResult == k_EResultOK,
			io_failure ? String("Steam replay attachment I/O failure") : steam_result_name(value->m_eResult));
		result["ugc_handle"] = static_cast<int64_t>(replay_upload_handle);
		result["remote_filename"] = replay_upload_remote_filename;
		finish_replay_upload(result);
	}

	void finish_replay_download(const Dictionary &result)
	{
		const int64_t request_id = replay_download_request_id;
		replay_download_request_id = 0;
		replay_download_handle = k_UGCHandleInvalid;
		replay_download_maximum_bytes = 0;
		owner->complete_leaderboard_replay_download(request_id, result);
	}

	void on_replay_downloaded(RemoteStorageDownloadUGCResult_t *value, bool io_failure)
	{
		if (replay_download_request_id == 0) return;
		if (io_failure || value->m_eResult != k_EResultOK || value->m_hFile != replay_download_handle || !SteamRemoteStorage()) {
			finish_replay_download(request_result(false, io_failure ? String("Steam replay download I/O failure") : steam_result_name(value->m_eResult)));
			return;
		}
		if (value->m_nSizeInBytes <= 0 || static_cast<int64_t>(value->m_nSizeInBytes) > replay_download_maximum_bytes) {
			finish_replay_download(request_result(false, "Downloaded replay is outside the allowed size."));
			return;
		}
		PackedByteArray bytes;
		bytes.resize(value->m_nSizeInBytes);
		const int32 read = SteamRemoteStorage()->UGCRead(value->m_hFile, bytes.ptrw(), value->m_nSizeInBytes, 0, k_EUGCRead_Close);
		if (read != value->m_nSizeInBytes) {
			finish_replay_download(request_result(false, "Steam returned an incomplete replay download."));
			return;
		}
		Dictionary result = request_result(true, "Replay bytes downloaded from Steam.");
		result["bytes"] = bytes;
		result["ugc_handle"] = static_cast<int64_t>(value->m_hFile);
		result["owner_steam_id"] = static_cast<int64_t>(value->m_ulSteamIDOwner);
		finish_replay_download(result);
	}

	void on_create_item(CreateItemResult_t *value, bool io_failure)
	{
		Dictionary result = request_result(!io_failure && value->m_eResult == k_EResultOK,
				io_failure ? String("Steam I/O failure") : steam_result_name(value->m_eResult));
		result["published_file_id"] = static_cast<int64_t>(value->m_nPublishedFileId);
		result["legal_agreement_required"] = value->m_bUserNeedsToAcceptWorkshopLegalAgreement;
		const int64_t request_id = create_request_id;
		create_request_id = 0;
		owner->complete_workshop_request(request_id, "create_item", result);
	}

	void on_submit_item(SubmitItemUpdateResult_t *value, bool io_failure)
	{
		Dictionary result = request_result(!io_failure && value->m_eResult == k_EResultOK,
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
		Dictionary result = request_result(value->m_eResult == k_EResultOK, steam_result_name(value->m_eResult));
		result["published_file_id"] = static_cast<int64_t>(value->m_nPublishedFileId);
		result["steam_result"] = static_cast<int64_t>(value->m_eResult);
		if (SteamUGC()) result["item_state"] = workshop_item_dictionary(SteamUGC(), value->m_nPublishedFileId);
		owner->publish_workshop_diagnostic("download_result", result);
		owner->complete_workshop_request(0, "download", result);
		owner->refresh_workshop_items();
	}

	void on_item_installed(ItemInstalled_t *value)
	{
		if (value->m_unAppID != owner->get_app_id()) return;
		Dictionary result = request_result(true, "item installed");
		result["published_file_id"] = static_cast<int64_t>(value->m_nPublishedFileId);
		if (SteamUGC()) result["item_state"] = workshop_item_dictionary(SteamUGC(), value->m_nPublishedFileId);
		owner->publish_workshop_diagnostic("item_installed", result);
		owner->complete_workshop_request(0, "item_installed", result);
		owner->refresh_workshop_items();
	}

	void on_subscribed(RemoteStoragePublishedFileSubscribed_t *value)
	{
		if (value->m_nAppID != owner->get_app_id()) return;
		Dictionary result = request_result(true, "item subscribed");
		result["published_file_id"] = static_cast<int64_t>(value->m_nPublishedFileId);
		if (SteamUGC()) result["item_state"] = workshop_item_dictionary(SteamUGC(), value->m_nPublishedFileId);
		owner->publish_workshop_diagnostic("item_subscribed", result);
		owner->complete_workshop_request(0, "item_subscribed", result);
		owner->refresh_workshop_items();
	}

	void on_unsubscribed(RemoteStoragePublishedFileUnsubscribed_t *value)
	{
		if (value->m_nAppID != owner->get_app_id()) return;
		Dictionary result = request_result(true, "item unsubscribed");
		result["published_file_id"] = static_cast<int64_t>(value->m_nPublishedFileId);
		if (SteamUGC()) result["item_state"] = workshop_item_dictionary(SteamUGC(), value->m_nPublishedFileId);
		owner->publish_workshop_diagnostic("item_unsubscribed", result);
		owner->complete_workshop_request(0, "item_unsubscribed", result);
		owner->refresh_workshop_items();
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
	ClassDB::bind_method(D_METHOD("refresh_workshop_items"), &MxtSteamService::refresh_workshop_items);
	ClassDB::bind_method(D_METHOD("track_workshop_item", "published_file_id"), &MxtSteamService::track_workshop_item);
	ClassDB::bind_method(D_METHOD("subscribe_workshop_item", "published_file_id"), &MxtSteamService::subscribe_workshop_item);
	ClassDB::bind_method(D_METHOD("unsubscribe_workshop_item", "published_file_id"), &MxtSteamService::unsubscribe_workshop_item);
	ClassDB::bind_method(D_METHOD("download_workshop_item", "published_file_id", "high_priority"), &MxtSteamService::download_workshop_item, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("open_workshop_item_page", "published_file_id"), &MxtSteamService::open_workshop_item_page);
	ClassDB::bind_method(D_METHOD("get_workshop_update_progress"), &MxtSteamService::get_workshop_update_progress);
	ClassDB::bind_method(D_METHOD("get_workshop_items"), &MxtSteamService::get_workshop_items);
	ClassDB::bind_method(
			D_METHOD("request_leaderboard_entries", "leaderboard_name", "request_type", "range_start", "range_end"),
			&MxtSteamService::request_leaderboard_entries,
			DEFVAL(1),
			DEFVAL(100));
	ClassDB::bind_method(D_METHOD("request_web_api_auth_ticket", "identity"), &MxtSteamService::request_web_api_auth_ticket);
	ClassDB::bind_method(D_METHOD("cancel_web_api_auth_ticket", "ticket_handle"), &MxtSteamService::cancel_web_api_auth_ticket);
	ClassDB::bind_method(D_METHOD("upload_leaderboard_replay", "leaderboard_name", "remote_filename", "expected_replay_sha256", "bytes"), &MxtSteamService::upload_leaderboard_replay);
	ClassDB::bind_method(D_METHOD("attach_existing_leaderboard_replay", "leaderboard_name", "ugc_handle"), &MxtSteamService::attach_existing_leaderboard_replay);
	ClassDB::bind_method(D_METHOD("download_leaderboard_replay", "ugc_handle", "maximum_bytes"), &MxtSteamService::download_leaderboard_replay);

	ADD_SIGNAL(MethodInfo("status_changed", PropertyInfo(Variant::DICTIONARY, "status")));
	ADD_SIGNAL(MethodInfo(
			"workshop_request_completed",
			PropertyInfo(Variant::INT, "request_id"),
			PropertyInfo(Variant::STRING, "operation"),
			PropertyInfo(Variant::DICTIONARY, "result")));
	ADD_SIGNAL(MethodInfo("workshop_items_changed", PropertyInfo(Variant::ARRAY, "items")));
	ADD_SIGNAL(MethodInfo(
			"workshop_diagnostic_event",
			PropertyInfo(Variant::STRING, "event"),
			PropertyInfo(Variant::DICTIONARY, "fields")));
	ADD_SIGNAL(MethodInfo(
			"leaderboard_request_completed",
			PropertyInfo(Variant::INT, "request_id"),
			PropertyInfo(Variant::DICTIONARY, "result")));
	ADD_SIGNAL(MethodInfo(
			"web_api_ticket_request_completed",
			PropertyInfo(Variant::INT, "request_id"),
			PropertyInfo(Variant::DICTIONARY, "result")));
	ADD_SIGNAL(MethodInfo("leaderboard_replay_upload_completed",
			PropertyInfo(Variant::INT, "request_id"), PropertyInfo(Variant::DICTIONARY, "result")));
	ADD_SIGNAL(MethodInfo("leaderboard_replay_download_completed",
			PropertyInfo(Variant::INT, "request_id"), PropertyInfo(Variant::DICTIONARY, "result")));
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
	ISteamUserStats *user_stats = SteamUserStats();
	if (!utils || !user || !friends || !ugc || !user_stats) {
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
	load_tracked_workshop_item_ids();
	UtilityFunctions::print(
			"MXT Steam: initialized app_id=", app_id,
			" steam_id=", static_cast<int64_t>(steam_id),
			" persona=", persona_name);
	publish_status();
	refresh_workshop_items();
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
		workshop_state->leaderboard_find_call.Cancel();
		workshop_state->leaderboard_download_call.Cancel();
		workshop_state->leaderboard_queue_head = 0;
		workshop_state->leaderboard_queue_count = 0;
		workshop_state->active_leaderboard_request = SteamWorkshopState::LeaderboardReadRequest();
		workshop_state->leaderboard_request_active = false;
		if (SteamUser() && workshop_state->web_api_ticket_handle != k_HAuthTicketInvalid) {
			SteamUser()->CancelAuthTicket(workshop_state->web_api_ticket_handle);
		}
		workshop_state->web_api_ticket_request_id = 0;
		workshop_state->web_api_ticket_handle = k_HAuthTicketInvalid;
		workshop_state->web_api_ticket_identity = String();
	}
	SteamAPI_Shutdown();
#endif

	initialized = false;
	set_process(false);
	clear_account_state();
	workshop_items.clear();
	tracked_workshop_item_ids.clear();
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

int64_t MxtSteamService::allocate_request_id()
{
	const int64_t request_id = next_request_id;
	if (next_request_id == INT64_MAX) {
		next_request_id = 1;
	} else {
		++next_request_id;
	}
	return request_id;
}

void MxtSteamService::complete_leaderboard_request(int64_t request_id, const Dictionary &result)
{
	call_deferred("emit_signal", StringName("leaderboard_request_completed"), request_id, result);
}

void MxtSteamService::complete_web_api_ticket_request(int64_t request_id, const Dictionary &result)
{
	call_deferred("emit_signal", StringName("web_api_ticket_request_completed"), request_id, result);
}

void MxtSteamService::complete_leaderboard_replay_upload(int64_t request_id, const Dictionary &result)
{
	call_deferred("emit_signal", StringName("leaderboard_replay_upload_completed"), request_id, result);
}

void MxtSteamService::complete_leaderboard_replay_download(int64_t request_id, const Dictionary &result)
{
	call_deferred("emit_signal", StringName("leaderboard_replay_download_completed"), request_id, result);
}

void MxtSteamService::complete_workshop_request(
		int64_t request_id,
		const String &operation,
		const Dictionary &result)
{
	call_deferred("emit_signal", StringName("workshop_request_completed"), request_id, operation, result);
}

void MxtSteamService::publish_workshop_items()
{
	emit_signal("workshop_items_changed", workshop_items.duplicate(true));
}

void MxtSteamService::load_tracked_workshop_item_ids()
{
	tracked_workshop_item_ids.clear();
	if (steam_id == 0) return;
	Ref<ConfigFile> config;
	config.instantiate();
	const Error error = config->load(TRACKED_WORKSHOP_ITEMS_PATH);
	if (error != OK && error != ERR_FILE_NOT_FOUND) {
		Dictionary diagnostic;
		diagnostic["path"] = TRACKED_WORKSHOP_ITEMS_PATH;
		diagnostic["error"] = static_cast<int64_t>(error);
		publish_workshop_diagnostic("tracked_items_load_failed", diagnostic);
		return;
	}
	const String section = String("account_") + String::num_uint64(steam_id);
	const Variant stored = config->get_value(section, "published_file_ids", Array());
	if (stored.get_type() != Variant::ARRAY) return;
	const Array ids = stored;
	for (int64_t i = 0; i < ids.size(); ++i) {
		const int64_t published_file_id = ids[i];
		if (published_file_id > 0 && !tracked_workshop_item_ids.has(published_file_id)) {
			tracked_workshop_item_ids.push_back(published_file_id);
		}
	}
	tracked_workshop_item_ids.sort();
	Dictionary diagnostic;
	diagnostic["published_file_ids"] = tracked_workshop_item_ids.duplicate();
	publish_workshop_diagnostic("tracked_items_loaded", diagnostic);
}

void MxtSteamService::save_tracked_workshop_item_ids()
{
	if (steam_id == 0) return;
	Ref<ConfigFile> config;
	config.instantiate();
	const Error load_error = config->load(TRACKED_WORKSHOP_ITEMS_PATH);
	if (load_error != OK && load_error != ERR_FILE_NOT_FOUND) {
		Dictionary diagnostic;
		diagnostic["path"] = TRACKED_WORKSHOP_ITEMS_PATH;
		diagnostic["error"] = static_cast<int64_t>(load_error);
		publish_workshop_diagnostic("tracked_items_save_failed", diagnostic);
		return;
	}
	const String section = String("account_") + String::num_uint64(steam_id);
	config->set_value(section, "published_file_ids", tracked_workshop_item_ids.duplicate());
	const Error save_error = config->save(TRACKED_WORKSHOP_ITEMS_PATH);
	Dictionary diagnostic;
	diagnostic["path"] = TRACKED_WORKSHOP_ITEMS_PATH;
	diagnostic["published_file_ids"] = tracked_workshop_item_ids.duplicate();
	diagnostic["error"] = static_cast<int64_t>(save_error);
	publish_workshop_diagnostic(save_error == OK ? "tracked_items_saved" : "tracked_items_save_failed", diagnostic);
}

void MxtSteamService::publish_workshop_diagnostic(const String &event, const Dictionary &fields)
{
#if defined(MXT_STEAMWORKS_ENABLED)
	workshop_trace(event, fields);
#endif
	emit_signal("workshop_diagnostic_event", event, fields.duplicate(true));
}

int64_t MxtSteamService::create_workshop_item()
{
	const int64_t request_id = allocate_request_id();
#if defined(MXT_STEAMWORKS_ENABLED)
	if (!initialized || !workshop_state || !SteamUGC()) {
		complete_workshop_request(request_id, "create_item", request_result(false, "Steam Workshop is unavailable."));
		return request_id;
	}
	if (workshop_state->create_request_id != 0 || workshop_state->submit_request_id != 0) {
		complete_workshop_request(request_id, "create_item", request_result(false, "Another Workshop publishing operation is active."));
		return request_id;
	}
	const SteamAPICall_t call = SteamUGC()->CreateItem(static_cast<AppId_t>(app_id), k_EWorkshopFileTypeCommunity);
	if (call == k_uAPICallInvalid) {
		complete_workshop_request(request_id, "create_item", request_result(false, "Steam rejected the Workshop create request."));
		return request_id;
	}
	workshop_state->create_request_id = request_id;
	workshop_state->create_call.Set(call, workshop_state, &SteamWorkshopState::on_create_item);
#else
	complete_workshop_request(request_id, "create_item", request_result(false, "Steamworks support was not compiled into this build."));
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
	const int64_t request_id = allocate_request_id();
#if defined(MXT_STEAMWORKS_ENABLED)
	if (!initialized || !workshop_state || !SteamUGC()) {
		complete_workshop_request(request_id, "submit_update", request_result(false, "Steam Workshop is unavailable."));
		return request_id;
	}
	if (published_file_id <= 0 || title.is_empty() || title.length() > 128 || description.length() > 8000 ||
			metadata.length() >= k_cchDeveloperMetadataMax || tags.is_empty() || tags.size() > 16) {
		complete_workshop_request(request_id, "submit_update", request_result(false, "Workshop metadata is missing or exceeds its supported limits."));
		return request_id;
	}
	if (workshop_state->create_request_id != 0 || workshop_state->submit_request_id != 0) {
		complete_workshop_request(request_id, "submit_update", request_result(false, "Another Workshop publishing operation is active."));
		return request_id;
	}
	const String use_content_path = absolute_path(content_path);
	const String use_preview_path = absolute_path(preview_path);
	if (!DirAccess::open(use_content_path).is_valid() || !FileAccess::file_exists(use_preview_path)) {
		complete_workshop_request(request_id, "submit_update", request_result(false, "Workshop content or preview path does not exist."));
		return request_id;
	}
	const Ref<FileAccess> preview_file = FileAccess::open(use_preview_path, FileAccess::READ);
	if (preview_file.is_null() || preview_file->get_length() >= WORKSHOP_PREVIEW_MAX_BYTES) {
		complete_workshop_request(request_id, "submit_update", request_result(false, "Steam Workshop preview images must be smaller than 1 MB."));
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
		complete_workshop_request(request_id, "submit_update", request_result(false, "Workshop visibility is invalid."));
		return request_id;
	}
	for (int64_t i = 0; i < tags.size(); ++i) {
		const String tag = tags[i];
		if (tag.is_empty() || tag.length() > 64) {
			complete_workshop_request(request_id, "submit_update", request_result(false, "Workshop tags must contain 1 to 64 characters."));
			return request_id;
		}
	}
	const UGCUpdateHandle_t handle = SteamUGC()->StartItemUpdate(
			static_cast<AppId_t>(app_id),
			static_cast<PublishedFileId_t>(published_file_id));
	if (handle == k_UGCUpdateHandleInvalid) {
		complete_workshop_request(request_id, "submit_update", request_result(false, "Steam could not start the Workshop update."));
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
		complete_workshop_request(request_id, "submit_update", request_result(false, "Steam rejected one or more Workshop update fields."));
		return request_id;
	}
	const CharString note_utf8 = change_note.utf8();
	const SteamAPICall_t call = SteamUGC()->SubmitItemUpdate(handle, note_utf8.get_data());
	if (call == k_uAPICallInvalid) {
		complete_workshop_request(request_id, "submit_update", request_result(false, "Steam rejected the Workshop submit request."));
		return request_id;
	}
	workshop_state->submit_request_id = request_id;
	workshop_state->update_handle = handle;
	workshop_state->submit_call.Set(call, workshop_state, &SteamWorkshopState::on_submit_item);
#else
	complete_workshop_request(request_id, "submit_update", request_result(false, "Steamworks support was not compiled into this build."));
#endif
	return request_id;
}

bool MxtSteamService::refresh_workshop_items()
{
#if defined(MXT_STEAMWORKS_ENABLED)
	const uint64_t refresh_start_usec = Time::get_singleton()->get_ticks_usec();
	if (!initialized || !SteamUGC()) {
		Dictionary unavailable;
		unavailable["initialized"] = initialized;
		unavailable["steam_ugc_available"] = SteamUGC() != nullptr;
		publish_workshop_diagnostic("refresh_rejected", unavailable);
		return false;
	}
	ISteamUGC *ugc = SteamUGC();
	const uint32 count = ugc->GetNumSubscribedItems(true);
	std::vector<PublishedFileId_t> ids(count);
	const uint32 received = count == 0 ? 0 : ugc->GetSubscribedItems(ids.data(), count, true);
	ids.resize(received);
	for (int64_t i = 0; i < tracked_workshop_item_ids.size(); ++i) {
		const PublishedFileId_t session_id = static_cast<PublishedFileId_t>(
				static_cast<int64_t>(tracked_workshop_item_ids[i]));
		bool already_present = false;
		for (const PublishedFileId_t id : ids) {
			if (id == session_id) {
				already_present = true;
				break;
			}
		}
		if (!already_present) ids.push_back(session_id);
	}
	Dictionary refresh_begin;
	refresh_begin["subscribed_count"] = static_cast<int64_t>(count);
	refresh_begin["received_count"] = static_cast<int64_t>(received);
	refresh_begin["tracked_item_ids"] = tracked_workshop_item_ids.duplicate();
	refresh_begin["merged_count"] = static_cast<int64_t>(ids.size());
	publish_workshop_diagnostic("refresh_begin", refresh_begin);
	Array refreshed_items;
	for (const PublishedFileId_t id : ids) {
		Dictionary item = workshop_item_dictionary(ugc, id);
		refreshed_items.push_back(item);
		publish_workshop_diagnostic("refresh_item", item);
	}
	const bool changed = refreshed_items != workshop_items;
	workshop_items = refreshed_items;
	publish_workshop_items();
	Dictionary refresh_end;
	refresh_end["duration_usec"] = static_cast<int64_t>(Time::get_singleton()->get_ticks_usec() - refresh_start_usec);
	refresh_end["published_item_count"] = workshop_items.size();
	refresh_end["changed"] = changed;
	publish_workshop_diagnostic("refresh_end", refresh_end);
	return true;
#else
	return false;
#endif
}

bool MxtSteamService::subscribe_workshop_item(int64_t published_file_id)
{
#if defined(MXT_STEAMWORKS_ENABLED)
	return initialized && published_file_id > 0 && SteamUGC() &&
			SteamUGC()->SubscribeItem(static_cast<PublishedFileId_t>(published_file_id)) != k_uAPICallInvalid;
#else
	return false;
#endif
}

bool MxtSteamService::track_workshop_item(int64_t published_file_id)
{
#if defined(MXT_STEAMWORKS_ENABLED)
	if (!initialized || published_file_id <= 0 || !SteamUGC()) return false;
	if (!tracked_workshop_item_ids.has(published_file_id)) {
		tracked_workshop_item_ids.push_back(published_file_id);
		tracked_workshop_item_ids.sort();
		save_tracked_workshop_item_ids();
	}
	return true;
#else
	return false;
#endif
}

bool MxtSteamService::unsubscribe_workshop_item(int64_t published_file_id)
{
#if defined(MXT_STEAMWORKS_ENABLED)
	if (!initialized || published_file_id <= 0 || !SteamUGC() ||
			SteamUGC()->UnsubscribeItem(static_cast<PublishedFileId_t>(published_file_id)) == k_uAPICallInvalid) {
		return false;
	}
	tracked_workshop_item_ids.erase(published_file_id);
	save_tracked_workshop_item_ids();
	return true;
#else
	return false;
#endif
}

bool MxtSteamService::download_workshop_item(int64_t published_file_id, bool high_priority)
{
#if defined(MXT_STEAMWORKS_ENABLED)
	Dictionary request;
	request["published_file_id"] = published_file_id;
	request["high_priority"] = high_priority;
	request["initialized"] = initialized;
	request["steam_ugc_available"] = SteamUGC() != nullptr;
	if (!track_workshop_item(published_file_id)) {
		request["accepted"] = false;
		request["reason"] = "invalid request or Steam Workshop unavailable";
		publish_workshop_diagnostic("download_request", request);
		return false;
	}
	ISteamUGC *ugc = SteamUGC();
	const PublishedFileId_t item_id = static_cast<PublishedFileId_t>(published_file_id);
	request["item_state_before"] = workshop_item_dictionary(ugc, item_id);
	const uint32 state = ugc->GetItemState(item_id);
	if ((state & (k_EItemStateDownloading | k_EItemStateDownloadPending)) != 0) {
		request["accepted"] = true;
		request["already_in_progress"] = true;
		request["item_state_after"] = request["item_state_before"];
		publish_workshop_diagnostic("download_request", request);
		return true;
	}
	const bool accepted = ugc->DownloadItem(item_id, high_priority);
	request["accepted"] = accepted;
	request["item_state_after"] = workshop_item_dictionary(ugc, item_id);
	publish_workshop_diagnostic("download_request", request);
	if (!accepted) return false;
	return true;
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

int64_t MxtSteamService::request_leaderboard_entries(
		const String &leaderboard_name,
		const String &request_type,
		int32_t range_start,
		int32_t range_end)
{
	const int64_t request_id = allocate_request_id();
#if defined(MXT_STEAMWORKS_ENABLED)
	if (!initialized || !workshop_state || !SteamUserStats()) {
		complete_leaderboard_request(request_id, request_result(false, "Steam leaderboards are unavailable."));
		return request_id;
	}
	if (leaderboard_name.is_empty() || leaderboard_name.length() > 128 || range_end < range_start ||
			range_start < -10000 || range_end > 10000 || range_end - range_start > 999) {
		complete_leaderboard_request(request_id, request_result(false, "Leaderboard request parameters are invalid."));
		return request_id;
	}
	ELeaderboardDataRequest data_request = k_ELeaderboardDataRequestGlobal;
	if (request_type == "around_user") {
		data_request = k_ELeaderboardDataRequestGlobalAroundUser;
	} else if (request_type == "friends") {
		data_request = k_ELeaderboardDataRequestFriends;
	} else if (request_type != "global") {
		complete_leaderboard_request(request_id, request_result(false, "Leaderboard request type is invalid."));
		return request_id;
	}
	SteamWorkshopState::LeaderboardReadRequest request;
	request.request_id = request_id;
	request.leaderboard_name = leaderboard_name;
	request.data_request = data_request;
	request.range_start = range_start;
	request.range_end = range_end;
	if (!workshop_state->enqueue_leaderboard_request(request)) {
		complete_leaderboard_request(request_id, request_result(false, "Leaderboard request queue is full."));
	}
#else
	complete_leaderboard_request(request_id, request_result(false, "Steamworks support was not compiled into this build."));
#endif
	return request_id;
}

int64_t MxtSteamService::request_web_api_auth_ticket(const String &identity)
{
	const int64_t request_id = allocate_request_id();
#if defined(MXT_STEAMWORKS_ENABLED)
	if (!initialized || !workshop_state || !SteamUser()) {
		complete_web_api_ticket_request(request_id, request_result(false, "Steam authentication is unavailable."));
		return request_id;
	}
	if (identity.is_empty() || identity.length() > 128) {
		complete_web_api_ticket_request(request_id, request_result(false, "Web API ticket identity is invalid."));
		return request_id;
	}
	if (workshop_state->web_api_ticket_request_id != 0) {
		complete_web_api_ticket_request(request_id, request_result(false, "Another Web API ticket request is active."));
		return request_id;
	}
	const CharString identity_utf8 = identity.utf8();
	const HAuthTicket handle = SteamUser()->GetAuthTicketForWebApi(identity_utf8.get_data());
	if (handle == k_HAuthTicketInvalid) {
		complete_web_api_ticket_request(request_id, request_result(false, "Steam rejected the Web API ticket request."));
		return request_id;
	}
	workshop_state->web_api_ticket_request_id = request_id;
	workshop_state->web_api_ticket_handle = handle;
	workshop_state->web_api_ticket_identity = identity;
#else
	complete_web_api_ticket_request(request_id, request_result(false, "Steamworks support was not compiled into this build."));
#endif
	return request_id;
}

bool MxtSteamService::cancel_web_api_auth_ticket(int64_t ticket_handle)
{
#if defined(MXT_STEAMWORKS_ENABLED)
	if (!initialized || ticket_handle <= 0 || !SteamUser()) return false;
	SteamUser()->CancelAuthTicket(static_cast<HAuthTicket>(ticket_handle));
	return true;
#else
	return false;
#endif
}

int64_t MxtSteamService::upload_leaderboard_replay(
		const String &leaderboard_name,
		const String &remote_filename,
		const String &expected_replay_sha256,
		const PackedByteArray &bytes)
{
	const int64_t request_id = allocate_request_id();
#if defined(MXT_STEAMWORKS_ENABLED)
	if (!initialized || !workshop_state || !SteamRemoteStorage() || !SteamUserStats()) {
		complete_leaderboard_replay_upload(request_id, request_result(false, "Steam replay storage is unavailable."));
		return request_id;
	}
	if (workshop_state->replay_upload_request_id != 0 || leaderboard_name.is_empty() || leaderboard_name.length() > 128 ||
			remote_filename.is_empty() || remote_filename.length() > 240 || expected_replay_sha256.length() != 71 ||
			!expected_replay_sha256.begins_with("sha256:") || bytes.is_empty() || bytes.size() > 64 * 1024 * 1024) {
		complete_leaderboard_replay_upload(request_id, request_result(false, "Replay upload parameters or state are invalid."));
		return request_id;
	}
	for (int index = 0; index < 8; ++index) {
		const String chunk = expected_replay_sha256.substr(7 + index * 8, 8);
		if (!chunk.is_valid_hex_number(false)) {
			complete_leaderboard_replay_upload(request_id, request_result(false, "Replay upload digest is invalid."));
			return request_id;
		}
		workshop_state->replay_upload_digest_words[index] = static_cast<uint32>(chunk.hex_to_int());
	}
	const CharString filename_utf8 = remote_filename.utf8();
	if (!SteamRemoteStorage()->FileWrite(filename_utf8.get_data(), bytes.ptr(), static_cast<int32>(bytes.size()))) {
		String message = "Steam could not write the replay to Remote Storage.";
		if (!SteamRemoteStorage()->IsCloudEnabledForApp()) {
			message = "Steam Cloud is not enabled for this app.";
		} else if (!SteamRemoteStorage()->IsCloudEnabledForAccount()) {
			message = "Steam Cloud is disabled for this Steam account.";
		} else {
			uint64 total_bytes = 0;
			uint64 available_bytes = 0;
			if (SteamRemoteStorage()->GetQuota(&total_bytes, &available_bytes) &&
					available_bytes < static_cast<uint64>(bytes.size())) {
				message = "Steam Cloud has insufficient storage quota (" +
						String::num_uint64(available_bytes) + " bytes available; " +
						String::num_int64(bytes.size()) + " required).";
			}
		}
		complete_leaderboard_replay_upload(request_id, request_result(false, message));
		return request_id;
	}
	const SteamAPICall_t call = SteamRemoteStorage()->FileShare(filename_utf8.get_data());
	if (call == k_uAPICallInvalid) {
		complete_leaderboard_replay_upload(request_id, request_result(false, "Steam rejected the replay share request."));
		return request_id;
	}
	workshop_state->replay_upload_request_id = request_id;
	workshop_state->replay_upload_leaderboard_name = leaderboard_name;
	workshop_state->replay_upload_remote_filename = remote_filename;
	workshop_state->replay_share_call.Set(call, workshop_state, &SteamWorkshopState::on_replay_file_shared);
#else
	complete_leaderboard_replay_upload(request_id, request_result(false, "Steamworks support was not compiled into this build."));
#endif
	return request_id;
}

int64_t MxtSteamService::attach_existing_leaderboard_replay(const String &leaderboard_name, int64_t ugc_handle)
{
	const int64_t request_id = allocate_request_id();
#if defined(MXT_STEAMWORKS_ENABLED)
	if (!initialized || !workshop_state || !SteamUserStats()) {
		complete_leaderboard_replay_upload(request_id, request_result(false, "Steam leaderboard replay attachment is unavailable."));
		return request_id;
	}
	const UGCHandle_t handle = static_cast<UGCHandle_t>(ugc_handle);
	if (workshop_state->replay_upload_request_id != 0 || leaderboard_name.is_empty() ||
			leaderboard_name.length() > 128 || handle == k_UGCHandleInvalid) {
		complete_leaderboard_replay_upload(request_id, request_result(false, "Existing replay attachment parameters or state are invalid."));
		return request_id;
	}
	workshop_state->replay_upload_request_id = request_id;
	workshop_state->replay_upload_leaderboard_name = leaderboard_name;
	workshop_state->replay_upload_handle = handle;
	workshop_state->replay_attach_existing = true;
	const CharString name_utf8 = leaderboard_name.utf8();
	const SteamAPICall_t call = SteamUserStats()->FindLeaderboard(name_utf8.get_data());
	if (call == k_uAPICallInvalid) {
		workshop_state->finish_replay_upload(request_result(false, "Steam rejected the replay leaderboard lookup."));
		return request_id;
	}
	workshop_state->replay_leaderboard_find_call.Set(
			call, workshop_state, &SteamWorkshopState::on_replay_leaderboard_found);
#else
	complete_leaderboard_replay_upload(request_id, request_result(false, "Steamworks support was not compiled into this build."));
#endif
	return request_id;
}

int64_t MxtSteamService::download_leaderboard_replay(int64_t ugc_handle, int64_t maximum_bytes)
{
	const int64_t request_id = allocate_request_id();
#if defined(MXT_STEAMWORKS_ENABLED)
	if (!initialized || !workshop_state || !SteamRemoteStorage()) {
		complete_leaderboard_replay_download(request_id, request_result(false, "Steam replay storage is unavailable."));
		return request_id;
	}
	if (workshop_state->replay_download_request_id != 0 || ugc_handle == 0 ||
			static_cast<UGCHandle_t>(ugc_handle) == k_UGCHandleInvalid ||
			maximum_bytes <= 0 || maximum_bytes > 64 * 1024 * 1024) {
		complete_leaderboard_replay_download(request_id, request_result(false, "Replay download parameters or state are invalid."));
		return request_id;
	}
	const UGCHandle_t handle = static_cast<UGCHandle_t>(ugc_handle);
	const SteamAPICall_t call = SteamRemoteStorage()->UGCDownload(handle, 0);
	if (call == k_uAPICallInvalid) {
		complete_leaderboard_replay_download(request_id, request_result(false, "Steam rejected the replay download request."));
		return request_id;
	}
	workshop_state->replay_download_request_id = request_id;
	workshop_state->replay_download_handle = handle;
	workshop_state->replay_download_maximum_bytes = maximum_bytes;
	workshop_state->replay_download_call.Set(call, workshop_state, &SteamWorkshopState::on_replay_downloaded);
#else
	complete_leaderboard_replay_download(request_id, request_result(false, "Steamworks support was not compiled into this build."));
#endif
	return request_id;
}
