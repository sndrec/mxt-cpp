#include "register_types.h"

#include "content/content_validator.h"
#include "content/content_package_io.h"
#include "content/content_catalog.h"
#include "content/content_record.h"
#include "content/content_load_result.h"
#include "content/content_sync_result.h"
#include "core/race_configuration.h"
#include "core/race_roster.h"
#include "core/custom_stamp_wire_codec.h"
#include "core/race_session_state.h"
#include "core/track_content_evidence.h"
#include "content/track_package_builder.h"
#include "car/car_authoring_session.h"
#include "car/car_draft_store.h"
#include "car/car_performance_analyzer.h"
#include "gamesim/gamesim.h"
#include "camera/fzgx_gameplay_camera.h"
#include "netcode/netcode_session.h"
#include "render/native_custom_stamp_image_builder.h"
#include "render/native_stamp_mesh_builder.h"
#include "replay/replay_stream.h"
#include "replay/replay_run_metadata.h"
#include "leaderboard/leaderboard_data.h"
#include "audio/opus_voice_codec.h"
#include "audio/spatial_audio_manager.h"
#include "platform/steam/steam_service.h"
#include "track/track_editor_curve.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

void initialize_gamesim_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	GDREGISTER_CLASS(TrackEditorCurve);
	GDREGISTER_CLASS(TrackEditorFloatCurve);
	GDREGISTER_CLASS(GameSim);
	GDREGISTER_CLASS(FzgxGameplayCamera);
	GDREGISTER_CLASS(NetcodeSession);
	GDREGISTER_CLASS(NativeCustomStampImageBuilder);
	GDREGISTER_CLASS(NativeStampMeshBuilder);
	GDREGISTER_CLASS(MxtReplayStream);
	GDREGISTER_CLASS(MxtReplayRunMetadata);
	GDREGISTER_CLASS(MxtLeaderboardEntry);
	GDREGISTER_CLASS(MxtLeaderboardQueryResult);
	GDREGISTER_CLASS(OpusVoiceCodec);
	GDREGISTER_CLASS(MxtSpatialAudioManager);
	GDREGISTER_CLASS(MxtSteamService);
	GDREGISTER_CLASS(MxtContentValidator);
	GDREGISTER_CLASS(MxtContentPackageIO);
	GDREGISTER_CLASS(MxtContentCatalog);
	GDREGISTER_CLASS(MxtContentRecord);
	GDREGISTER_CLASS(MxtContentLoadResult);
	GDREGISTER_CLASS(MxtContentCatalogDelta);
	GDREGISTER_CLASS(MxtWorkshopSyncItem);
	GDREGISTER_CLASS(MxtWorkshopSyncResult);
	GDREGISTER_CLASS(MxtRaceConfiguration);
	GDREGISTER_CLASS(MxtRaceRoster);
	GDREGISTER_CLASS(MxtCustomStampWireCodec);
	GDREGISTER_CLASS(MxtRaceSessionState);
	GDREGISTER_CLASS(MxtTrackContentEvidence);
	GDREGISTER_CLASS(MxtTrackPackageBuilder);
	GDREGISTER_CLASS(MxtCarAuthoringSession);
	GDREGISTER_CLASS(MxtCarDraftStore);
	GDREGISTER_CLASS(MxtCarPerformanceAnalyzer);
}

void uninitialize_gamesim_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
}

extern "C" {
	// Initialization.
	GDExtensionBool GDE_EXPORT gamesim_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, const GDExtensionClassLibraryPtr p_library, GDExtensionInitialization* r_initialization) {
		godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

		init_obj.register_initializer(initialize_gamesim_module);
		init_obj.register_terminator(uninitialize_gamesim_module);
		init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

		return init_obj.init();
	}
}
