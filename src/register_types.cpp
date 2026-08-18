#include "register_types.h"

#include "content/content_validator.h"
#include "content/content_package_io.h"
#include "content/content_catalog.h"
#include "content/track_package_builder.h"
#include "car/car_authoring_session.h"
#include "car/car_draft_store.h"
#include "car/car_performance_analyzer.h"
#include "gamesim/gamesim.h"
#include "camera/fzgx_gameplay_camera.h"
#include "netcode/netcode_session.h"
#include "render/native_stamp_mesh_builder.h"
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
	GDREGISTER_CLASS(NativeStampMeshBuilder);
	GDREGISTER_CLASS(OpusVoiceCodec);
	GDREGISTER_CLASS(MxtSpatialAudioManager);
	GDREGISTER_CLASS(MxtSteamService);
	GDREGISTER_CLASS(MxtContentValidator);
	GDREGISTER_CLASS(MxtContentPackageIO);
	GDREGISTER_CLASS(MxtContentCatalog);
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
