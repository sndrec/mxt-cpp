# F-Zero GX drift-plasma pipeline

This document maps the shipped GFZE01 drift-plasma effect from machine state to
the GameCube GPU. It is the source of truth for any future MaxX Throttle port.
It describes observed executable behavior, including calculations that appear
redundant or accidental. It does not describe the current MaxX Throttle
implementation and is not permission to change it.

## Evidence and naming

The analysis uses the GFZE01 executable and the original assets extracted from
`A:\F-Zero GX (USA).nkit.iso`, cross-checked against the decompilation at
`A:\programs\smb1-decomp\src-fzgx`.

Original asset hashes:

- `init/efcmdl.gma`: `D297965428E3603509FD1AD6EAB3F158BF0528C7663D4E611180468EDA042CD4`
- `init/efcmdl.tpl`: `A9278A8F7FFEC23234A2291D2E0A960BFFA68DBD92D8BF7B303B52A94CD63FE2`

Names such as `current_position`, `velocity`, and `suspension_fr` below are
supported by data flow or independently named machine-physics code. Unknown
fields retain an offset rather than receiving a speculative name.

## End-to-end summary

The effect is not a conventional emitter feeding a GPU particle system. The
complete path is:

1. A persistent type-1 effect record associated with each machine runs
   `fn_1_68284` through the shared effect scheduler.
2. `fn_1_68284` derives a normalized machine-lateral vector from the machine's
   physical forward vector and surface normal, then calls `fn_1_6AF70`.
3. `fn_1_6AF70` examines four suspension/contact records. It measures each
   corner's drift/no-force state and signed lateral displacement over the last
   simulation tick, and may allocate one to three type-2 records
   (`ET_DRIFT_PTCL`) for an eligible corner.
4. The shared effect scheduler decrements lifetime, snapshots current position
   into history, and calls the type-2 update `fn_1_5962C`.
5. The shared draw scheduler calls `fn_1_59770`. That function turns the
   history/current position pair into a camera-facing, view-space streak
   transform and queues a 64-byte draw record plus a copied 64-byte model
   record.
6. `fn_1_9FA18` establishes the GX state, binds the texture from the copied
   model, and calls `fn_1_9F164` for every queued streak.
7. `fn_1_9F164` CPU-transforms one square quad, writes four positions and four
   UVs directly to the GX FIFO, and relies on additive texture modulation for
   the final plasma image.

The effect's appearance therefore depends on all of the following together:

- actual per-corner motion and suspension-state flags;
- the shared 60 Hz effect scheduler and its history-copy ordering;
- the deliberately one-velocity-step-back initial position;
- a very short three-to-seven-tick lifetime;
- CPU construction of a motion-aligned, camera-facing quad;
- the exact `EFF_RENSFREA01` texture and additive GX state.

## 1. Owner effect and lateral direction

The type-1 init/update dispatch entries are wrappers:

- init table index 1 -> `fn_1_5942C` -> `fn_1_680F8`;
- update table index 1 -> `fn_1_5944C` -> `fn_1_68284`.

`fn_1_680F8` associates the record with a machine, captures initial machine
state, and creates related child effects. Drift-plasma emission happens in the
owner's regular update, not in the machine renderer.

`fn_1_68284` obtains the machine's physical basis and transforms local
`(0, 0, -1)` into world space. It also obtains the machine's current surface
normal. It calculates:

```text
forward = physical_basis * (0, 0, -1)
lateral = normalize(cross(forward, surface_normal))
```

It passes that normalized `lateral` vector to `fn_1_6AF70`.

This means the emission test uses the machine's real physical orientation and
surface-relative lateral direction. It is not global-axis slip, input steer,
or an explicit drift-state flag.

## 2. Corner records and eligibility

`fn_1_6AF70` walks four 0x5c-byte records beginning at machine offset `0x244`.
The independently reconstructed steering code names these in memory order:

| Record | Countdown used by emitter | Meaning |
| --- | ---: | --- |
| `+0x244` | 4 | front-right suspension/contact |
| `+0x2a0` | 3 | front-left suspension/contact |
| `+0x2fc` | 2 | back-right suspension/contact |
| `+0x358` | 1 | back-left suspension/contact |

Relevant fields within each record are:

| Relative offset | Use |
| --- | --- |
| `+0x00` | state flags |
| `+0x14..+0x1c` | previous corner position |
| `+0x20..+0x28` | current corner position |
| `+0x38..+0x40` | corner vector used to remove normal motion and add outward velocity |

The record is eligible only when state bit `0x2` is clear and state bit `0x4`
is set. The machine-physics reconstruction names these flags
`FZGX_TC_NO_FORCE = 0x2` and `FZGX_TC_DRIFT = 0x4`. This is a per-corner drift
flag, not only a machine-wide drift classification. The separate
`FZGX_TC_NO_CONTACT = 0x8` bit is not part of this emitter condition.

Emission frequency is also conditional:

```text
if active_view_count < 2 and machine.input_slot != -1:
    examine corners this tick
else:
    examine corners only when global_frame_counter % 5 == 0
```

Machine byte `+0x475` is the entrant's local input/player slot; camera and race
setup code set it to `-1` for entrants not assigned to a local player. Thus a
locally controlled entrant in a one-view race is sampled every tick. AI and
other unassigned entrants are sampled every fifth tick, and a multi-view race
uses the five-tick cadence for everyone.

For each eligible corner:

```text
delta = current_corner_position - previous_corner_position
signed_motion = dot(delta, lateral)
```

Side selection is exact:

- positive `signed_motion` may emit only from front-right and back-right;
- negative or zero `signed_motion` may emit only from front-left and back-left;
- the negative side negates the value before calculating intensity.

Thus a lateral slide activates the two corners on only one side of the machine.
The front two records set effect flag `0x4000`; the renderer later halves their
quad scalar.

The side-selected corner still does not emit until the magnitude reaches one
world unit per tick. At exactly one it enters the emitter with zero intensity;
below one it does nothing. For an emitting corner:

```text
intensity = clamp(abs(signed_motion) - 1.0, 0.0, 1.0)
```

The function sums the four intensities and stores `0.5 * sum` in a per-entrant
array at `lbl_1_bss_6F3A4`. Machine audio update `FUN_8024a8b0` reads that value
while any corner has `DRIFT` set, converts it to a bounded byte target, eases a
per-view byte one step toward the target, and sends it to the drift-sound
commands. This side channel controls associated audio intensity; it does not
feed back into particle simulation or rendering.

## 3. Random source and spawn construction

All random samples use the shared effect RNG `fn_1_584AC`:

```text
seed = seed * 0x41c64e6d + 0x3039
sample15 = (seed >> 16) & 0x7fff
rand01 = sample15 / 32767.0
```

The emitter zeroes one 0xe8-byte command template and sets:

- type `+0x0c = 2` (`ET_DRIFT_PTCL`);
- entrant index `+0x18` from the owner;
- view mask `+0x1a = 3`;
- model pointer `+0x34` from the loaded `efcmdl.gma` model table at byte
  offset `0xe8`, which is model index 29.

For each accepted corner:

```text
emit_strength = clamp(intensity, 0.3, 1.0)
raw_delta = current_corner_position - previous_corner_position

velocity = 0.93 * raw_delta
velocity -= corner_vector * dot(velocity, corner_vector)
velocity += random_vector(-0.4, +0.4 per axis)
velocity += corner_vector * (0.8 * emit_strength)
velocity += lateral * signed_side * emit_strength
```

The projection removal assumes `corner_vector` is normalized; there is no
division by its squared length.

Particle count is based on the unclipped length of the corner delta, clamped to
five only for this decision:

```text
corner_speed = clamp(length(raw_delta), 0.0, 5.0)
count = 1                  when corner_speed <= 2
count = 2                  when 2 < corner_speed <= 4
count = 3                  when corner_speed > 4
```

### Shipped interpolation overwrite

The multi-particle loop calculates a remaining-distance ratio and an
interpolated point between the current and previous corner positions. The
executable then overwrites all three interpolated coordinates with the exact
current corner coordinates before allocation. No later instruction consumes
the interpolated point.

This is not an interpretation of intent. Exact GFZE01 behavior is that all
particles in the burst begin from the current corner position, subject only to
their independent random jitter. A port must not preserve the discarded
interpolation merely because it appears more sensible.

Each record then receives:

```text
life = trunc(60 * (0.05 + 0.08 * intensity * rand01))
position = current_corner_position + random_vector(-0.4, +0.4) - velocity

target_width = 0.5 * (0.5 + 0.3 * rand01)       # [0.25, 0.40]
current_width = 0.5 * (0.4 + 0.2 * intensity * rand01)
                                                    # [0.20, 0.30] at intensity 1

color_scale = 0.5 + 0.3 * rand01
red = 0.8 * color_scale                           # [0.40, 0.64]
green = 0.5 * color_scale                         # [0.25, 0.40]
blue = 0.5 * color_scale                          # [0.25, 0.40]
alpha = 1.0 at packing time
```

RNG consumption order is deterministic and significant. A corner that passes
the threshold first consumes three samples for velocity jitter. Every spawned
particle then consumes, in order, one sample for life, three for position
jitter, one for target width, one for current width, one shared color-scale
sample, and finally one sample in `fn_1_59514` for current damping. Thus an
emitting corner consumes `3 + 8*count` samples. All three RGB channels use the
same color-scale sample.

At maximum intensity, integer truncation produces a lifetime of three through
seven ticks. The continuous upper limit is below eight. Lower intensity narrows
the upper end.

The loop also subtracts two from a remaining-distance accumulator after every
particle. Because that accumulator only feeds the overwritten interpolation,
it has no effect on the allocated record.

## 4. Allocation and first-tick ordering

`fn_1_58F50` allocates from the first shared bank of 190 effect records:

1. reject allocation during either of two globally blocked states;
2. scan from slot zero for the first inactive record;
3. copy all 0xe8 bytes of the command;
4. set the active byte and pool index;
5. call the init callback selected by effect type;
6. assign an incrementing signed-short handle, wrapping negative values to
   zero.

It never replaces an active record.

The type-2 init `fn_1_59514` leaves a supplied nonzero lifetime intact. Through
`fn_1_8652C` it reads the machine's `speed_kmh` field at offset `0x17c`, then
initializes:

```text
current_damping = 0.05 * rand01
target_damping = 0.07 + speed_kmh / 20000.0
```

If a caller supplied zero life, the init would instead generate
`trunc(60 * (0.1 + rand01))`; the drift emitter does not take that path.

The shared scheduler `fn_1_58694` walks active records in slot order. For each
record it:

1. decrements remaining life;
2. destroys and deactivates the record immediately if life becomes zero;
3. otherwise, for ordinary active state, copies current position `+0x3c` into
   history `+0x60`;
4. calls the type-specific update.

An allocation made by an owner while that same bank is being traversed updates
later in the same tick only if the new slot is after the scheduler's current
cursor. This is a property of the actual first-free pool, not a guaranteed
special newborn update. The usual contiguous owner/particle layout makes a
same-tick first update common, but the scheduler remains slot-order dependent.

On that common path, a stored lifetime `L` produces updates and visible queue
submissions at remaining-life values `L-1` down through `1`: normally `L-1`
visible simulation states. The record is removed on the tick that would change
life from one to zero. A newborn allocated behind the current cursor can reach
the draw pass before its first scheduler history snapshot; that is a possible
consequence of pool ordering, not a separate intended spawn path.

## 5. Per-tick particle simulation

The type-2 update `fn_1_5962C` first copies current position into history again,
then executes:

```text
velocity.y -= 0.03

factor = 1.0 - current_damping
velocity *= factor
position += velocity

current_damping += 0.05 * (target_damping - current_damping)
current_width += 0.1 * (target_width - current_width)
```

The `y` acceleration is in GX world coordinates; it is not transformed through
the machine or surface basis.

After the scheduler has already decremented life, fading occurs only when
`life < 3`:

```text
rgb *= 1.0 - 1.0 / (life + 1.0)
```

Consequently life 2 multiplies RGB by `2/3`, life 1 multiplies it by `1/2`, and
the next tick destroys the particle before update or draw. Alpha remains 255;
the fade is RGB energy under additive blending, not conventional alpha fading.

The initial spawn at `corner + jitter - velocity`, followed by a damped velocity
step, is what places the first ordinary current/history segment around the
emitting corner. Treating the stored initial position as the desired visible
spawn point changes the whole streak.

For the common same-tick first update, the exact first-step relation is useful:

```text
P0 = corner + position_jitter - V0
V1 = (V0 + (0, -0.03, 0)) * (1 - initial_damping)
P1 = P0 + V1
history = P0
current = P1
```

Before the draw callback's small XY endpoint expansion, its extrapolated-tail
construction makes:

```text
current - tail = 1.5 * (P1 - P0) = 1.5 * V1
```

So the visible ribbon axis directly reflects one and a half times the current
damped per-tick velocity. It does not accumulate a multi-frame trail. The
one-step-back spawn advances the first current point back near the corner while
leaving the extrapolated tail behind it.

## 6. Draw callback: point history to ribbon transform

The type-2 draw callback is `fn_1_59770`. It obtains the machine's physical
basis and both camera frames. It transforms history with the previous
world-to-view matrix and current position with the current world-to-view
matrix:

```text
H = world_to_view(history_position)
C = world_to_view(current_position)
T = 1.5 * H - 0.5 * C
```

`T` is an extrapolated tail, not merely the stored history point. GX's camera
looks along negative view Z; the callback rejects the record when either
`C.z >= 0` or `T.z >= 0`.

For a nondegenerate projected segment, with `w = current_width`:

```text
dxy = normalize((C - T).xy)
T.xy -= dxy * (0.1 * w)
C.xy += dxy * (0.1 * w)

midpoint = 0.5 * (T + C)
segment_length = length(C - T)       # full 3D length after XY expansion
```

The orientation helper derives:

```text
pitch = atan2(T.y - C.y,
              sqrt((C.x - T.x)^2 + (C.z - T.z)^2))
yaw = atan2(C.x - T.x, C.z - T.z)
```

GX's current 3x4 matrix is row-major storage representing `R * p + t`.
Translate, rotate, and scale helpers post-multiply the current matrix. The
constructed packet matrix is therefore, in call order:

```text
M = Translate(midpoint)
M = M * RotateY(yaw)
M = M * RotateX(pitch)
M = M * Scale(1, 1, segment_length / w)
M = M * RotateX(+90 degrees)
```

The callback then calls a helper sometimes mislabeled by decompilers as
returning translation. Its exact operation is:

```text
camera_relative = -transpose(M.linear_3x3) * M.translation
twist = atan2(camera_relative.x, camera_relative.z)
M = M * RotateY(twist)
```

This is the camera-facing twist about the already aligned ribbon axis. Because
the linear part already contains nonuniform scale, this helper is not a general
affine inverse; reproducing it with a mathematically cleaner inverse changes
the result.

If `(C - T).x == 0` and `(C - T).y == 0`, the callback follows this separate
fallback call sequence:

```text
M = world_to_view
M = M * Translate(current_world_position)
M.linear_3x3 = machine_physical_basis
M = M * RotateX(+90 degrees)
```

The two matrix helpers make this stranger than their names first suggest.
`mtxa_translate` post-translates the current matrix, so the origin after the
second line is `world_to_view * current_world_position`. `mtxa_from_mtx` then
copies only the supplied matrix's nine basis elements; it deliberately leaves
that transformed origin untouched. Consequently, the final basis is the raw
machine physical basis, not `world_to_view.linear * machine_physical_basis`.
This is the exact shipped fallback, even though mixing a view-space origin with
that basis is unusual. The branch avoids normalizing a zero projected
direction and must remain a separately matched case rather than being replaced
by an epsilon direction or a conventional billboard basis.

Finally it writes a 64-byte queue packet:

- `+0x00`: scalar `8 * current_width`, halved when effect flag `0x4000` is set;
- `+0x08..+0x37`: the 3x4 matrix `M`;
- `+0x3c`: packed `(u8)(255*r), (u8)(255*g), (u8)(255*b), 255`.

It enqueues that packet with the particle's 64-byte model record through
`fn_1_9F914`.

## 7. Queue and CPU geometry

The render queue owns parallel fixed buffers:

- 0x4000 bytes for 256 64-byte draw packets;
- 0x4000 bytes for 256 copied 64-byte model records;
- a separate 0x1800-byte buffer used by the queue's other primitive path.

`fn_1_9F914` drops a request when its count reaches 256. Otherwise it copies the
packet and model record into parallel slots and increments the count. The model
copy matters: the queued texture object is taken from model offset `+0x24`, not
looked up again from the live effect record during the flush.

`fn_1_58994` resets the queue before scanning and invoking effect draw
callbacks. Later in the frame, `fn_1_545B8` calls `fn_1_9FA18` to flush it.

Before the loop, `fn_1_9FA18` loads the current view matrix as GX position
matrix zero. For every record it binds the `GXTexObj` copied at model offset
`+0x24`, restores the current CPU matrix stack to the view matrix, and calls
`fn_1_9F164`.

`fn_1_9F164` multiplies the current CPU matrix by the packet matrix and
transforms a local square of half-extent `s = packet.scalar`:

| Vertex | Local position | UV |
| ---: | --- | --- |
| 0 | `(-s, -s, 0)` | `(0, 0)` |
| 1 | `(+s, -s, 0)` | `(1, 0)` |
| 2 | `(+s, +s, 0)` | `(1, 1)` |
| 3 | `(-s, +s, 0)` | `(0, 1)` |

It emits `GX_QUADS` with four vertices. Positions and UVs are direct FIFO
attributes. There are no normals and no per-vertex colors. The function also
emits a preceding eight-vertex primitive containing zero-valued attributes;
that executable behavior is real, but it contributes no visible plasma
geometry and its hardware-side purpose is not named in the decomp.

The result is one CPU-transformed textured quad per effect record. It is not a
billboard point sprite, trail mesh assembled across particles, or road decal.

## 8. Exact asset selection

`fn_1_41048` loads `init/efcmdl.tpl` and `init/efcmdl.gma`. The GMA contains 64
models. The emitter's resource-table byte offset `0xe8` is `0xe8 / 8 = 29`, so
the selected model is:

```text
model 29: EFF_RENSFREA01
GCMF offset: 0x34a0
material count: 1
material offset: 0x34e0
texture index: 18
material flags: 0
```

This corrects an earlier false lead that identified the resource as
`EFF_SMOKE_D` / texture 26. Model 41 is `EFF_SMOKE_D`; it is not used by
`ET_DRIFT_PTCL`.

Texture 18 in `efcmdl.tpl` is:

```text
format: CMPR
dimensions: 128 x 128
mip levels: 8
data offset: 0x12b00
```

The decoded texture is a neutral grayscale starburst/flame shape. Its
reddish-orange appearance is produced by the per-particle RGB multiplier, not
stored orange pixels. Material flags zero select clamp wrapping in both S and
T for this GMA material format.

## 9. Exact GX pipeline state

For this queue, `fn_1_9FA18` configures:

- culling: none;
- color channels: zero, so no lighting channel participates;
- one texcoord generator and one TEV stage;
- direct position and texcoord-0 vertex attributes only (`0x2200` mask);
- texture map 0 / texcoord 0;
- constant color register K0 loaded from the packet RGBA;
- fog disabled through the executable's cached fog-state wrapper;
- Z test enabled, comparison `LEQUAL`, Z writes disabled;
- blend mode `BLEND`, source factor `ONE`, destination factor `ONE`, logic op
  `CLEAR` (logic operation is inactive in blend mode).

The single TEV stage uses:

```text
color inputs: ZERO, KONST, TEXC, ZERO
alpha inputs: ZERO, KONST, TEXA, ZERO
operation: ADD, zero bias, scale 1, clamp, output PREV
K color selector: K0 RGB
K alpha selector: K0 A
```

Therefore the source fragment is:

```text
src.rgb = texture.rgb * particle.rgb
src.a   = texture.a   * particle.a       # particle.a is 1.0
```

With `ONE + ONE` blending, the relevant framebuffer operation is:

```text
dst.rgb = clamp(src.rgb + dst.rgb)
```

Depth is tested against existing geometry but the plasma does not modify the
depth buffer. Its visible softness comes from the CMPR texture and additive
energy, not alpha interpolation against the destination.

## 10. Record layout used by this path

The shared effect record is 0xe8 bytes. Confirmed fields used by this effect are:

| Offset | Meaning in this path |
| ---: | --- |
| `0x00` | active/state byte |
| `0x02` | pool slot index |
| `0x04` | assigned effect handle |
| `0x08` | effect flags; `0x4000` halves rendered scalar |
| `0x0c` | effect type (`2`) |
| `0x10` | remaining life |
| `0x18` | entrant index |
| `0x1a` | view/player mask |
| `0x1c..0x24` | RGB floats |
| `0x28` | current width |
| `0x2c` | target width |
| `0x34` | model/template pointer |
| `0x3c..0x44` | current position |
| `0x48..0x50` | velocity |
| `0x60..0x68` | history position |
| `0x94` | current damping |
| `0x98` | target damping |

## 11. Port-critical invariants

Any future implementation should be evaluated against these invariants before
visual tuning begins:

1. Emission requires each suspension corner's `DRIFT` bit with `NO_FORCE`
   clear, then uses that corner's real current-minus-previous motion projected
   onto a surface-relative machine lateral vector.
2. Only the right or left pair selected by signed lateral motion emits.
3. All particles in a burst use the current corner, because GX overwrites its
   own interpolation.
4. Initial stored position is one full pre-damping velocity step behind the
   jittered corner.
5. History is a scheduler snapshot, not an independently integrated trail
   point.
6. Lifetime is measured in 60 Hz effect ticks and is normally only three to
   seven ticks.
7. The draw tail is `1.5*history - 0.5*current` in view space.
8. History and current position must use their matching previous/current camera
   matrices. A single current camera matrix incorrectly turns the followed
   machine's full forward travel into streak length.
9. The rendered object is one transformed square whose long dimension comes
   from matrix scale, followed by GX's specific camera-facing twist.
10. The local square half-extent is `8*width` (or `4*width` for front corners),
   while longitudinal scale is `segment_length/width`; these factors interact
   and must not be collapsed into an arbitrary length/width pair.
11. The correct asset is model 29 `EFF_RENSFREA01`, texture 18, clamp-wrapped.
12. Color is a dim red-biased multiplier over a neutral texture, accumulated
    additively with depth test and no depth write.
13. Simulation and render transformations must use a single consistent unit,
    tick, coordinate, and matrix convention. Compensating for a mismatch by
    tuning particle size, speed, or lifetime will only conceal it.

## 12. Source map

All offsets are GFZE01.

| Stage | Function / data | Primary source |
| --- | --- | --- |
| type-1 wrappers | `fn_1_5942C`, `fn_1_5944C` | `matching/src/main_rel/fn_1_5942C.c` |
| owner init | `fn_1_680F8` | `matching/src_behavioral/main_rel/fn_1_680F8.c` |
| lateral derivation / caller | `fn_1_68284` | `matching/build/GFZE01/main_rel/asm/auto_00_00068284_text.s` |
| emitter | `fn_1_6AF70`, REL `.text 0x6af70`, size `0x934` | `matching/build/GFZE01/main_rel/asm/auto_00_00069BCC_text.s` |
| particle init | `fn_1_59514` | `matching/src_behavioral/main_rel/fn_1_59514.c` |
| particle update | `fn_1_5962C` / runtime `FUN_801ffdec` | `slices/functions/801ffdec__FUN_801ffdec.c` |
| particle draw | `fn_1_59770` / runtime `FUN_801fff30` | `slices/functions/801fff30__FUN_801fff30.c` |
| allocation | `fn_1_58F50` | `matching/src/main_rel/fn_1_58F50.c` |
| effect update scheduler | `fn_1_58694` / runtime `FUN_801fee54` | `slices/functions/801fee54__FUN_801fee54.c` |
| effect draw scheduler | `fn_1_58994` / runtime `FUN_801ff154` | `slices/functions/801ff154__FUN_801ff154.c` |
| RNG | `fn_1_584AC` | `matching/src/main_rel/fn_1_584AC.c` |
| machine speed accessor | `fn_1_8652C` | `matching/src/main_rel/fn_1_864FC.c` |
| drift-intensity audio consumer | runtime `FUN_8024a8b0` | `slices/functions/8024a8b0__FUN_8024a8b0.c` |
| dispatch tables | `lbl_1_data_1D1D8`, `lbl_1_data_1D2EC` | `matching/build/GFZE01/main_rel/asm/auto_04_0001C5D4_data.s` |
| queue allocation/reset/enqueue | `fn_1_9F7DC`, `fn_1_9F8FC`, `fn_1_9F914` | `matching/src/main_rel/fn_1_9F7DC.c`, `fn_1_9F8FC.c`, `fn_1_9F914.c` |
| queue flush | `fn_1_9FA18` | `matching/build/GFZE01/main_rel/asm/auto_00_0008E450_text.s` |
| CPU quad callback | `fn_1_9F164` / runtime `FUN_80245804` | `slices/functions/80245804__FUN_80245804.c` |
| queue flush caller | `fn_1_545B8` | `matching/src/main_rel/fn_1_545B8.c` |
| effect resources | `fn_1_41048` | `matching/src/main_rel/fn_1_41048.c` |
| draw translation helper | `mtxa_translate` | `slices/functions/line078279__mtxa_translate.c` |
| draw basis-copy helper | `mtxa_from_mtx` | `slices/functions/line077625__mtxa_from_mtx.c` |
| remaining matrix implementation | matrix helpers around runtime `0x8006d...` | `line__.rel-Mapped_Cache.c` |
| suspension order / bit use | `fn_1_253A4`, `fn_1_8677C`, `FZGX_TC_*` | `matching/src_behavioral/main_rel/fn_1_253A4.c`, `matching/src/main_rel/fn_1_8677C.c`, `core_c/include/fzgx/sim.h` |
| GX position-matrix load | `fn_80038C5C` | `matching/build/GFZE01/asm/auto_01_800055E0_text.s` |
| fog-state disable | `fn_800720B0(0)` | same DOL assembly file |
| direct vertex descriptor mask | `fn_8007245C(0x2200)` | same DOL assembly file |
| GMA/TPL format cross-check | model/material and texture parsers | `GxUtils-master/GxUtils/LibGxFormat/Gma`, `.../Tpl` |

The source map is deliberately function-addressed so future work can return to
the executable path instead of re-deriving behavior from the current port.
