# Runtime ownership and lifecycle

`mxto/main.tscn` is the composition root. Its `main.gd` script wires concrete
scene-owned nodes once in `_ready`; callers use those owners directly rather
than routing work through compatibility facades.

## Main scene owners

| Owner | Source | Owns | Lifecycle boundary |
| --- | --- | --- | --- |
| Main | `mxto/main.gd` | menu navigation, composition, process/physics-loop orchestration | wires owners in `_ready`; delegates race creation/destruction and returns to menu |
| GameSim / ServerGameSim | `src/gamesim` | native client and authoritative simulation state | configured for a race, stepped at 60 Hz, destroyed at race transition |
| RaceSessionController | `mxto/core/race_session_controller.gd` | world instantiation, players, local racer index, race transitions | `initialize` once; `start_race`; `begin_transition`; `destroy_world` |
| DebugRuntimeController | `mxto/core/debug_runtime_controller.gd` | command-line automation, profiling, debug labels/screenshots | `initialize` once; configure from process arguments; reset profiling per launch/race |
| ReplayController | `mxto/replay/replay_controller.gd` | replay catalog, recording, playback, seeking, launch requests | initialize/scan at startup; configure or record per race; finalize on race end |
| RaceAudioController | `mxto/audio/race_audio_controller.gd` | race music, vehicle audio, finish timing and ducking | initialize once; configure content and reset at each race |
| TrackContentController | `mxto/track/track_content_controller.gd` | official/local/Workshop track catalog and evidence | scan at startup/refresh; resolve selected track before race construction |
| VehicleContentController | `mxto/vehicle/vehicle_content_controller.gd` | vehicle catalog, definitions, package evidence and stamp integration | initialize with Steam/stamp owners; refresh catalog outside simulation; resolve evidence before admission |
| LobbyController | `mxto/ui/lobby_controller.gd` | lobby controls, track list, start/settings requests | initialize once; refresh while in lobby; clear when leaving it |
| LobbyChibiController | `mxto/ui/lobby_chibi_controller.gd` | lobby-only car previews, nameplates and local hover/input | initialize once; update in lobby; clear on race/menu transition |
| CommunicationController | `mxto/ui/communication_controller.gd` | lobby input, bounded text history, race chat and voice-status presentation | initialize once; reset between sessions; close race chat on transitions |
| SpectatorController | `mxto/ui/spectator_controller.gd` | local spectator/DNF state, focus changes and spectator camera input | configure at race start; update during race; reset at teardown |
| RacePresentationController | `mxto/ui/race_presentation_controller.gd` | HUD, results overlay, nametags, notifications, medals and sticker pools | configure/reset per race; update each process frame; clear on transition |
| PlaytestLobbyProbe | `mxto/netplay/playtest_lobby_probe.gd` | opt-in lobby load instrumentation | dormant normally; enabled only by load-test launch arguments |

## Network owner tree

`NetworkManager` owns connection/session membership and composes the following
children. RPCs live on the child that owns their state.

| Owner | Source | Owns | Lifecycle boundary |
| --- | --- | --- | --- |
| NetworkManager | `mxto/netplay/network_manager.gd` | host/join/disconnect, peer membership, race phase and cross-owner lifecycle consequences | enters on host/join; refreshes child contexts as roster/phase changes; `reset_race_state`; disconnect teardown |
| LobbySettingsController | `mxto/netplay/lobby_settings_controller.gd` | synchronized race options, CPU roster, local latency and lobby metrics | initialize once; context refresh in lobby/race; preserve or clear settings on session reset |
| RaceAdmissionController | `mxto/netplay/race_admission_controller.gd` | version/content readiness, admission stages, coordinated start time | reset before admission; initialize states; advance peer stages; begin simulation once ready |
| InputTransportController | `mxto/netplay/input_transport_controller.gd` | native netcode sessions, input packets, ticks, RTT/jitter, prediction and rollback replay | reset/configure per race; start client/authoritative simulation; step in packet/tick loops |
| StateTransferController | `mxto/netplay/state_transfer_controller.gd` | state snapshots, compression, fragmentation/FEC and restore delivery | reset/configure per race phase; send during join/recovery; clear on race reset |
| RaceResultsController | `mxto/netplay/race_results_controller.gd` | finish/DNF/elimination state, force-end deadline and final placements | reset at race start; collect events during race; publish final results before transition |
| NetworkTelemetryController | `mxto/netplay/network_telemetry_controller.gd` | interval counters, CSV rows, packet/state samples and per-peer snapshots | initialize once; start only when requested; sample/reset counters outside packet-critical loops |
| CustomStampNetworkController | `mxto/netplay/custom_stamp_network_controller.gd` | validated custom-stamp manifests/blobs and peer transfer state | catalog/session setup; bounded transfer while connected; clear on disconnect |
| ProximityVoiceChat | `mxto/netplay/proximity_voice_chat.gd` | voice capture, packet freshness, per-peer playback and spatialization | ready once; active while connected/racing; reset streams and peer state on teardown |

## Lifecycle sequence

1. `Main._ready` initializes persistent owners and scans content catalogs.
2. Menu/lobby activity updates catalogs, settings, peer membership and readiness;
   no race-only world is retained between sessions.
3. Admission validates version/content evidence and coordinates a start phase.
4. `RaceSessionController.start_race` creates the world and configures simulation,
   presentation, audio, spectator, replay and network owners.
5. The physics loop submits/receives input through `InputTransportController` and
   advances `GameSim`; presentation, audio, replay and telemetry consume results
   from their owners without taking simulation ownership.
6. Finish/DNF/elimination events accumulate in `RaceResultsController`; replay
   and presentation finalize the visible outcome.
7. Transition closes communication, destroys the race world, resets network race
   state and returns to lobby/menu ownership. Disconnect additionally clears
   peer transfer, voice and custom-stamp session state.

Native implementation details follow the domain map in the root README. This
document is intentionally an ownership index, not duplicate implementation
documentation.
