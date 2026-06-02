# F-ZERO GX Vehicle Audio Notes

This note is the durable map for vehicle-local SFX porting. It is meant to survive
thread compaction. Keep it evidence-oriented: source function, observed control,
confirmed sample, and current MaxX Throttle behavior.

## Scope

Vehicle audio here means sounds emitted by a machine or by immediate machine
actions/interactions: engine loops, airborne wind, strafing, brake scrub,
suspension/contact, landing, attacks, dash/jump plates, mines, collision, energy
restore, breakdown/retire, and terrain loops.

Out of scope for this pass: announcer, music, UI, crowd/ambient track SFX, and
manual boost reverse engineering beyond the per-machine sample property already
present in MaxX.

## Confirmed Sample Anchors

User-confirmed anchors:

- `brake.wav`
- `dash_plate.wav`
- `mine.wav`
- `sideattack.wav`
- `strafe.wav`
- `thrust_on.wav`
- `PACK1_169`: `thrust_on`
- `PACK1_174`: `strafe`
- `PACK1_175`: `sideattack`

Working-but-not-fully-proven anchors from prior extraction/listening:

- `zero_hp.wav`: likely breakdown/zero-HP onset.
- `boom.wav`: suspected transition from zero HP to retired/stopped.
- `suspension_contact.wav`: extracted from GX and triggered by suspension contact
  onset in decomp, but not strongly recognized by ear.
- `PACK1-191.wav`: user listening says this sounds like the original GX drift
  noise. Current MaxX wires it as the provisional drift loop sample because GX
  program `6` is named `SND_PACK1_DRIFT`, but exact program sample membership is
  still not decoded.

## GX Musyx Control Command Shape

Relevant decomp sources:

- `A:\programs\smb1-decomp\src-fzgx\slices\functions\80063090__FUN_80063090.c`
- `A:\programs\smb1-decomp\src-fzgx\slices\functions\80067894__FUN_80067894.c`
- `A:\programs\smb1-decomp\src-fzgx\slices\functions\8024943c__FUN_8024943c.c`
- `A:\programs\smb1-decomp\src-fzgx\slices\functions\801dbd30__FUN_801dbd30.c`

`FUN_8024943c(slot, command, value)` is a gated wrapper around
`FUN_80063090(slot, command, value)`.

Observed continuous-control commands:

- `-0x5b000000`: select/start Musyx program for slot.
- `-0x5af00000`: volume-like control, encoded as 7-bit control.
- `-0x5ae00000`: pan-like control, signed `-0x40..0x3f`.
- `-0x5a000000`: speed/pitch-like control, 7-bit plus high-bit behavior.
- `-0x59000000` / `-0x58f00000`: extra signed controls used in camera mode 9.
- `-0x5ac00000`: track-center/roadside-ish control in `FUN_801ae55c`.

`FUN_80067894(event_id)` enqueues a Musyx event command. One-shot vehicle events
are usually `0xa909xxxx`, while continuous slot control commands are synthesized
from the `FUN_80063090` command path.

## GX Sound-Test ICS Program Names

Primary source:

- `A:\programs\smb1-decomp\src-fzgx\slices\functions\line323665__fz_test_sound_tick.c`
- `A:\programs\smb1-decomp\src-fzgx\line__.rel-Mapped_Cache.bin`

The sound-test menu has an `ICS` category. Its start path calls
`FUN_80063090(0, -0x5b000000, program_id)`, the same program-select command used
by vehicle loop init. The PACK1 ICS string block in the mapped cache begins around
raw offset `0x31e269` and gives these names in index order:

| Program | Sound-test name | Vehicle interpretation |
| --- | --- | --- |
| `0` | `SND_PACK1_ICS_0` | Mid-weight primary machine engine |
| `1` | `SND_PACK1_ICS_0_COPY1` | Sound-test-only/copy, not selected by vehicle init |
| `2` | `SND_PACK1_ICS_0_COPY2` | Sound-test-only/copy, not selected by vehicle init |
| `3` | `SND_PACK1_ICS_0_COPY3` | Sound-test-only/copy, not selected by vehicle init |
| `4` | `SND_PACK1_LIGHT_MACHINE` | Light primary machine engine |
| `5` | `SND_PACK1_HEAVY_MACHINE` | Heavy primary machine engine |
| `6` | `SND_PACK1_DRIFT` | Drift/contact loop, driven from suspension contact state |
| `7` | `SND_PACK1_JUMP` | Jump/airborne loop, driven from airborne state and air tilt |
| `8` | `SND_PACK1_INTERIOR` | Not selected by observed vehicle init path |
| `9` | `SND_PACK1_REVERSE_GRAVITY` | Terrain/state layer gated by terrain bit `0x02000000` |
| `10` | `SND_PACK1_DIRT_ZONE` | Terrain/state layer gated by terrain bit `0x20000000` |

Important correction: programs `9` and `10` are not generic extra engine layers.
The runtime drives them using the same volume/speed control shape as the primary
engine slot, but their sound-test names and gates identify them as special
terrain/state layers.

Still open: this table names the Musyx programs, but it does not by itself decode
each program's internal sample/keymap membership. Exact `PACK1-###.wav` ownership
still needs a bank/program decode or live render comparison.

`A:\programs\smb1-decomp\src-fzgx\fzgx-iso\files\snd\normal_se\PACK1.bin`
starts with `gcaxDTPK` and does not expose these program names as plain strings.
The visible names above come from the sound-test metadata in the mapped cache,
not from the PACK sample blob. Do not treat nearby extracted WAV numbering as
proof of program membership without an actual DTPK/MusyX decode.

## GX Continuous Slot Layout

Primary source:

- `A:\programs\smb1-decomp\src-fzgx\slices\functions\801dbd30__FUN_801dbd30.c`

At race/audio init:

```c
for slots 0..3:
  if machine.weight > FLOAT_8030438c: program = 5
  else if machine.weight > FLOAT_80304388: program = 0
  else program = 4
  FUN_8024943c(slot, -0x5b000000, program)

FUN_8024943c(4, -0x5b000000, 10)
FUN_8024943c(5, -0x5b000000, 9)
FUN_8024943c(7, -0x5b000000, 6)
FUN_8024943c(6, -0x5b000000, 7)

for slots 0..15:
  volume/speed controls are zeroed
```

Interpretation:

- Slots `0..3`: nearest/camera machine primary engine program.
  - Program `4`: light machines.
  - Program `0`: middle-weight machines.
  - Program `5`: heavy machines.
- Slot `4`: program `10` (`SND_PACK1_DIRT_ZONE`), gated during runtime.
- Slot `5`: program `9` (`SND_PACK1_REVERSE_GRAVITY`), gated during runtime.
- Slot `6`: program `7` (`SND_PACK1_JUMP`), airborne/jump loop.
- Slot `7`: program `6` (`SND_PACK1_DRIFT`), drift/contact loop.

Important consequence: GX does not simply play every discovered "engine-ish"
sample all the time. It selects one primary engine program by weight, and it has
terrain/state loops that are driven separately from the primary engine.

## GX Continuous Runtime Update

Primary source:

- `A:\programs\smb1-decomp\src-fzgx\slices\functions\801ae55c__FUN_801ae55c.c`

Runtime flow:

1. Sort entrants by distance from the current listener/camera reference.
2. Keep up to `DAT_803cb604` active machine audio slots, normally capped around 4.
3. For camera/primary entrant:
   - Compute distance attenuation and pan via `FUN_8024a3a4`.
   - Compute speed control from `g_load_vector_speed() * fz::high_speec_sfx_threshold`,
     clamped to byte/control range.
   - If machine state does not include `0x880`, drive the selected primary slot:
     - `slot -0x5af00000 = volume`
     - `slot -0x5ae00000 = pan`
     - `slot -0x5a000000 = speed`
   - Otherwise zero volume/speed for that slot.
4. Call supporting vehicle-audio functions:
   - `FUN_8024a8b0`: airborne wind and suspension/contact loop.
   - `FUN_80249c9c`: attack, zero HP, thrust-on, race-start, strafe start/stop,
     and other machine-state one-shots.
   - `FUN_80249bd4`: state `0x400` one-shot.
   - `FUN_8024b208`: racetrack scratch/facing flag one-shots.
5. For terrain/state loop programs:
   - Program `10` (`SND_PACK1_DIRT_ZONE`) on slot `4` is driven only when
     `FUN_8022cbe4(entrant) >> 0x1d & 1` is set and machine state does not have
     `0x80`.
   - Program `9` (`SND_PACK1_REVERSE_GRAVITY`) on slot `5` is driven only when
     `FUN_8022cbe4(entrant) >> 0x19 & 1` is set and machine state does not have
     `0x80`.
   - `FUN_8022cbe4(entrant)` returns `g_active_machine_pointers[entrant].terrain_state`.
   - Both use the same volume and speed controls as the primary engine slot when
     active, and are intermittently zeroed otherwise.

## Airborne Wind

Primary source:

- `A:\programs\smb1-decomp\src-fzgx\slices\functions\8024a8b0__FUN_8024a8b0.c`

Slot/program:

- Slot `6`, program `7`, sound-test name `SND_PACK1_JUMP`.

Gate:

- Only the chosen active airborne machine slot.
- `speed > 5`.
- `FUN_801e6648() != 0`.
- `(machine_state & 0x880) == 0`.
- `(machine_state & 2) != 0` (`AIRBORNE`).

Controls:

```c
if air_tilt >= 0:
  control = 30.0 + 1.616666666 * air_tilt
else:
  control = 30.0 - (-0.6) * air_tilt

slot 6 speed/pitch control = control
slot 6 volume control = param_4 from distance/attenuation
```

Stop path:

- When airborne-related state transitions out, slot `6` speed and volume controls
  are set to `0`.

Current MaxX:

- Uses two local streams `air_0` and `air_1`.
- Uses air tilt for pitch and speed control for pitch ratio.
- Current volume is fixed `0 dB`.
- This is an approximation of one Musyx program with multiple internal layers.
  Sound-test evidence names the GX program as `JUMP`, but sample membership and
  internal curves for program `7` still need decoding before stronger claims
  about `air_0`/`air_1` correctness.

## Suspension/Contact Loop

Primary source:

- `A:\programs\smb1-decomp\src-fzgx\slices\functions\8024a8b0__FUN_8024a8b0.c`

Slot/program:

- Slot `7`, program `6`, sound-test name `SND_PACK1_DRIFT`.

Gate:

- Counts suspension corners whose state has bit `4`.
- Chooses one active machine slot with any such contact.

One-shot:

- On contact count rising from zero for the slot, it plays `0xa9092900` after
  setting per-sound volume control from active slot attenuation.

Loop controls:

```c
contact_target = byte(FLOAT_80307a80 * DAT_803cb6e4[entrant]) // FLOAT_80307a80 = 127.0
DAT_803cb527[slot] ramps by 1 toward contact_target
volume_control = min(0x7f, param_4 + 0x32), with special zero handling

slot 7 speed/pitch-ish control = DAT_803cb527[slot]
slot 7 volume control = volume_control
```

`DAT_803cb6e4[entrant]` producer:

- Primary source:
  `A:\programs\smb1-decomp\src-fzgx\slices\functions\802116fc__FUN_802116fc.c`
- Caller `8020ea1c` builds `param_2` as a lateral tangent from machine forward
  and track normal:
  - `local_94 = basis_physical * (0, 0, -1)`, because
    `FLOAT_80306298 = -1.0`.
  - `param_2 = normalize(-cross(local_94, track_normal))`.
- `802116fc` walks `machine.suspension_FR/FL/BR/BL` (`machine + 0x244`,
  stride `0x5c`) and only processes corners whose state has bit `4`.
  It explicitly skips corners whose state has bit `2`.
- For each active corner, it computes:
  - `corner_delta = corner.pos - corner.pos_old`
  - `lateral_delta = dot(corner_delta, param_2)`
  - FR/BR accept positive lateral delta; FL/BL accept negative lateral delta and
    flip it by multiplying by `-1.0`.
  - contribution is `clamp(lateral_delta_same_side - 1.0, 0.0, 1.0)`.
- After all four corners:
  - `DAT_803cb6e4[entrant] = 0.5 * contribution_sum`.
- Interpretation: this is a drift/contact lateral slip intensity, not vehicle
  speed. It rises as drift corners move farther sideways across the track plane
  and falls as that side-slip unwinds.

Stop path:

- If no contact or invalid state, slot `7` speed and volume controls are set to `0`.

Current MaxX:

- Treats drift/contact as any corner with `TILTSTATE::DRIFT` (`0x4`), matching
  the GX suspension-state bit used by `FUN_8024a8b0`.
- Plays `suspension_contact.wav` one-shot on drift/contact rising edge.
- Plays `drift_loop` as `PACK1-191.wav`.
- Computes the target control from the MaxX equivalent of GX's
  `DAT_803cb6e4`: per-drift-corner lateral displacement along the machine/track
  lateral tangent, side-filtered by corner, thresholded at `1.0`, capped per
  corner, half-summed, and scaled by `127.0`.
- Ignores MaxX corners that also have `TILTSTATE::AIRBORNE`, matching the
  producer's `(state & 2) == 0` condition.
- Because MaxX's `TILTSTATE::DRIFT` is broader/stickier than GX's exact
  suspension bit, the loop only plays while the computed target control or the
  ramped control is nonzero.
- Ramps the loop control byte by `1` per update toward that target.
- Sends the ramped control to pitch/speed, and sends a full local volume control
  to volume. This fixes an earlier MaxX mistake where the `+0x32`-style volume
  behavior was effectively applied to pitch instead.
- `PACK1-191.wav` has been removed from the heavy-engine table so it only plays
  through this drift/contact layer.

## Primary Engine

Primary sources:

- `A:\programs\smb1-decomp\src-fzgx\slices\functions\801dbd30__FUN_801dbd30.c`
- `A:\programs\smb1-decomp\src-fzgx\slices\functions\801ae55c__FUN_801ae55c.c`
- `A:\programs\smb1-decomp\src-fzgx\slices\functions\line323665__fz_test_sound_tick.c`

Confirmed from decomp:

- Primary engine is one selected Musyx program per active machine slot:
  - light: program `4`, `SND_PACK1_LIGHT_MACHINE`
  - mid: program `0`, `SND_PACK1_ICS_0`
  - heavy: program `5`, `SND_PACK1_HEAVY_MACHINE`
- Speed control ramps by max step `4` in GX's surrounding state logic.
- Volume and pan are derived from listener distance/position by `FUN_8024a3a4`.
- Programs `9` and `10` are not primary engine. They are named
  `REVERSE_GRAVITY` and `DIRT_ZONE` in sound test and are terrain-state loops.
- The always-on primary engine layer is therefore exactly one of programs
  `0`, `4`, or `5` for each active audible machine slot.

Current MaxX:

- Uses `mxt_audio_select_gx_engine_program(stat_weight)` with these sample tables:
  - light: `PACK1-189`, `PACK1-190`, `PACK1-237`, `PACK1-185`
  - mid: `PACK1-185`, `PACK1-186`, `PACK1-187`, `PACK1-236`, `PACK1-240`
  - heavy: `PACK1-250`, `PACK1-251`, `PACK1-188`
- Applies decoded-looking Musyx volume/pitch curves and a `-13 dB` trim per layer.
- Starts engine when car is audible and not in starting countdown. GX source does
  not show a separate low-speed start gate for the primary engine program.
- Does not currently model GX terrain/state slots `4`/`5` as separately gated
  programs `10`/`9`.

Engine correctness risk:

- The code evidence proves our behavior shape is wrong if any current "primary"
  sample table entries actually belong to programs `6`, `7`, `9`, or `10`.
- `PACK1-191` was removed from the heavy engine table after the GX drift evidence
  and user listening check.
- The current implementation layers up to five samples unconditionally for one
  machine. GX may do that inside one Musyx program, but the source does not prove
  these exact samples all belong to the selected primary program.
- GX source does not show a primary-engine low-speed gate equivalent to MaxX's
  explicit `speed > 4 km/h` start condition. GX starts/selects the program at
  init and controls audibility through volume/speed controls, so the MaxX speed
  gate should be treated as suspect.

Required before changing engine tables:

1. Decode Musyx program `0`, `4`, and `5` membership from PACK/sample metadata
   or live render comparison.
2. Split MaxX engine playback into:
   - primary engine program by weight,
   - drift/contact program `6`,
   - jump/airborne program `7`,
   - reverse-gravity program `9`,
   - dirt-zone program `10`.
3. Only after membership is known, adjust sample registrations and per-layer trim.

Implementation-safe behavior target, even before exact sample membership:

- Play exactly one primary engine program/layer set per active vehicle, selected
  by machine weight (`0`, `4`, or `5`).
- Keep the primary engine active as a loop selected at vehicle/audio init time;
  drive it with volume, pan, and speed controls instead of repeatedly starting
  and stopping it from a low-speed threshold.
- Do not mix drift/contact, jump/airborne, reverse-gravity, or dirt-zone samples
  into primary engine tables.
- Drive dirt-zone/reverse-gravity loops from terrain state, not machine weight.

## Strafe Roll Sound

Primary source:

- `A:\programs\smb1-decomp\src-fzgx\slices\functions\80249c9c__FUN_80249c9c.c`

Gate:

- Active audible slot must have nonzero attenuation byte.
- `abs(machine->strafe_visual_roll_angle) >= 100`.
- `speed > FLOAT_80307a5c` (same low-speed-ish threshold family as `5 km/h` checks).
- Previous stored roll state for that audio slot must be zero.

Events:

- Start: `0xa9091b00`.
- Stop: when roll magnitude falls below threshold and stored roll state is nonzero,
  play `0xa9091c00` and clear stored roll.
- Also stopped/cleared on zero-HP transition.

Confirmed sample:

- `PACK1_174` is strafe, not `66`/`67`.

Current MaxX:

- Computes a GX-like `strafe_visual_roll` from stat/input/speed and plays a loop
  `strafe` while `abs(roll) >= 100` and speed > `5`.
- Stop is implemented as stopping the loop only; no separate stop sample is used.
  This matches the current understanding after `strafe_stop.wav` was questioned:
  do not reintroduce a stop sample unless `0xa9091c00` is decoded and recognized
  as a real audible stop event that belongs here.

## Attacks, Race Start, Thrust-On, Zero HP

Primary source:

- `A:\programs\smb1-decomp\src-fzgx\slices\functions\80249c9c__FUN_80249c9c.c`

Events observed:

- Spin attack rising edge (`machine_state & 0x8`): `0xa9090e00`.
- Side attack rising edge (`machine_state & 0x20000`): `0xa9091d00`.
- Zero HP rising edge (`machine_state & 0x80`, excluding `B10`): `0xa9091100`.
- Race just began (`machine_state & 0x100000`): `0xa9091300`.
- Just tapped accel (`machine_state & 0x80000`, when race-start bit not set):
  `0xa9091400`.
- State `0x10000` rising edge: `0xa9091c00` plus `0xa9091600`.
- `FUN_8022cbcc(entrant) & 0x10`, every 8 frames: `0xa9092e00` and `0xa9091200`
  (likely periodic damaged/zero-HP or related state; not decoded).

Confirmed samples:

- `PACK1_175` is side attack.
- `PACK1_169` is thrust-on.

Current MaxX:

- Plays `spinattack` on `SPINATTACKING` rising edge.
- Plays `sideattack` on `SIDEATTACKING` rising edge.
- Plays `zero_hp` on `ZEROHP` rising edge when not `B10`.
- Plays `race_start` while `RACEJUSTBEGAN_Q` is set.
- Plays `thrust_on` on `JUSTTAPPEDACCEL` rising edge unless race-start is active.

Risks:

- Race-start is currently checked as level rather than rising edge. GX code plays
  while the bit is observed inside this per-tick function, but the source bit may
  itself be a one-frame flag.
- `0xa9091c00` appears in several stop/clear paths and should not be guessed as
  a normal user-facing sample until decoded.

## Landing

Primary sources:

- `A:\programs\smb1-decomp\src-fzgx\slices\functions\8024a064__FUN_8024a064.c`
- MaxX landing physics flags in `src/car/physics_car.cpp`.

GX gate:

- `machine_state & 0x10` (`JUSTLANDED`) must be set.
- `speed > 5`.
- Active slot attenuation byte must be nonzero.
- A per-entrant debounce/holdoff byte must be zero.

GX events:

- If not `B10`: set volume control to `attenuation + 0x32` (clamped), then play
  `0xa9090800`.
- If `B10`: set volume control to raw attenuation, then play `0xa9090700`.
- A small per-entrant counter prevents repeated immediate landing sounds.

Current MaxX:

- Plays `landing` or `landing_b10` on `JUSTLANDED` with speed > `5`.
- Does not currently model the GX per-entrant debounce or attenuation+`0x32`
  landing-volume behavior.

User concern:

- Vehicle landing still sounds wrong. This should be revisited after loop/engine
  mapping, with `0xa9090700` and `0xa9090800` decoded to concrete samples.

## Collision And Damage One-Shots

Primary sources:

- `A:\programs\smb1-decomp\src-fzgx\slices\functions\80249610__FUN_80249610.c`
- `A:\programs\smb1-decomp\src-fzgx\slices\functions\802494a4__FUN_802494a4.c`
- `A:\programs\smb1-decomp\src-fzgx\slices\functions\8024a2c0__FUN_8024a2c0.c`

`FUN_80249610`:

- Finds active slot for entrant.
- Requires slot attenuation byte nonzero.
- Ignores machines with state bit `0x800`.
- Scales input strength by `FLOAT_80307a4c`, clamps it, and chooses one of:
  - `0xa9090000`
  - `0xa9090200`
  - `0xa9090500`
  - `0xa9090600`
- Prior verified extraction names describe these as collision tiers.

`FUN_802494a4`:

- Sets volume from active slot attenuation.
- If `param_2 == 0`, plays machine/pilot-dependent entries from
  `null_ARRAY_80307994` or `null_ARRAY_80307a38`.
- Else plays `0xa9090b00`.

`FUN_8024a2c0`:

- If active, non-`0x280` machine, active slot attenuation nonzero, and `param_2 == 0`,
  plays `0xa9090f00`.

Current MaxX:

- Uses `last_hit_tick` and `last_hit_sfx_strength` to select four collision samples.
- Plays `collision_light_secondary` additionally for tier 0.
- This is an approximation; GX has more state-specific collision/damage events
  than the current four-tier mapping.

## Dash Plate, Jump Plate, Mine, Terrain

Primary sources:

- `A:\programs\smb1-decomp\src-fzgx\slices\functions\801ae55c__FUN_801ae55c.c`
- `A:\programs\smb1-decomp\src-fzgx\slices\functions\8024b208__FUN_8024b208.c`
- `A:\programs\smb1-decomp\src-fzgx\slices\functions\8024bbc8__FUN_8024bbc8.c`

Current MaxX:

- Dash plate:
  - Plays `dash_plate_secondary` and `dash_plate` on `JUST_HIT_DASHPLATE` rising
    edge, both at `-5 dB`.
  - `dash_plate.wav` is confirmed.
- Jump plate:
  - Plays `jump_plate` on rising `TERRAIN::JUMP` if speed > `850 km/h`.
  - GX source observed a `speed > FLOAT_850` gate for `0xa90a0700` in
    `FUN_801ae55c`; likely jump/very-high-speed track action, but this still
    needs sample decode.
- Mine:
  - Plays `mine` on rising terrain mine bit.
  - `mine.wav` is confirmed.
- Dirt/lava:
  - Current MaxX plays loop approximations on terrain bits using `PACK1-197` and
    `PACK1-196`.
  - GX terrain/trackside loop controls exist in `FUN_801ae55c`, including
    `-0x5ac00000` and global control calls, but this is not fully mapped.

## Energy Restore

Current MaxX:

- Plays `energy_restore` while `TERRAIN::RECHARGE` is active and fades/ramp-stops
  after leaving the strip.
- Pitch ramps from `pitstop_time`.
- Final volume is currently `energy_volume_base + 15 dB`.

User instruction:

- Keep current energy restore sample and behavior, except for the explicitly
  requested volume adjustment already made. Do not use energy restore as a
  testbed for GX reverse engineering right now.

## MaxX Audio Architecture

Sources:

- `B:\programming\mxt-cpp\src\mxt_core\spatial_audio_manager.h`
- `B:\programming\mxt-cpp\src\mxt_core\spatial_audio_manager.cpp`
- `B:\programming\mxt-cpp\mxto\main.gd`

Architecture:

- `MxtSpatialAudioManager` owns vehicle and world emitters.
- One-shots use each emitter's `AudioStreamPlayer3D` with
  `AudioStreamPolyphonic`.
- Loops use dedicated child `AudioStreamPlayer3D` instances stored as
  `LoopStream`, keyed by logical loop id.
- Loop metadata is not mutated in C++; streams are played as imported Godot audio
  resources. This fixed the previously inaudible loop bug.
- `mxto/main.gd` registers SFX resources and assigns per-vehicle manual boost
  SFX from `CarDefinition.manual_boost_sfx`.
- Local player has an `AudioListener3D` on the vehicle so local vehicle SFX do not
  vary with chase camera distance.

Important current MaxX SFX registrations:

- `gx_engine_185..190`, `236`, `237`, `240`, `250`, `251` point to
  `res://sfx/vehicle/thrust/PACK1-*.wav`.
- `drift_loop` points to `res://sfx/vehicle/thrust/PACK1-191.wav`.
- `strafe` points to confirmed `res://sfx/vehicle/strafe.wav`.
- `sideattack` points to confirmed `res://sfx/vehicle/sideattack.wav`.
- `thrust_on` points to confirmed `res://sfx/vehicle/thrust_on.wav`.

## Known Current Divergences To Fix After Program Decode

1. Engine may be too loud/wrong because MaxX unconditionally layers all samples in
   each weight table, while GX selects exactly one primary engine program by
   weight (`0`, `4`, or `5`) and separately drives drift/jump/terrain programs.
2. Drift loop volume currently uses full local volume control, while GX uses
   `min(0x7f, param_4 + 0x32)` from the active slot attenuation byte.
3. Program `7` is named `SND_PACK1_JUMP` and driven by airborne state/air tilt;
   current `air_0`/`air_1` mapping and curves are approximations.
4. Landing should be revisited with decoded `0xa9090700` and `0xa9090800`, plus
   the GX debounce and volume-control behavior.
5. Strafe stop should not be represented by an invented `strafe_stop.wav` unless
   `0xa9091c00` is decoded and confirmed as a strafe stop sample.
6. Collision/damage has more GX event IDs than the current four-tier collision
   mapping.

## Recommended Next Work Order

1. Decode Musyx sample/keymap membership for programs `0`, `4`, `5`, `6`, `7`,
   `9`, and `10`.
2. Rebuild MaxX loop layout to mirror GX slots semantically:
   - primary engine by weight,
   - drift/contact program `6`,
   - jump/airborne program `7`,
   - reverse-gravity program `9`,
   - dirt-zone program `10`.
3. Fix engine sample tables and remove any sample that belongs to
   drift/jump/terrain layers from primary engine tables.
4. Then revisit landing sample IDs and gates.
