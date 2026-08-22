# Garage livery customization

## Goal

Add a garage where players can choose a car, edit its paint colours, and place up to 16 layered body stamps. Stamps are not the current emote stickers; they are persistent livery decals projected onto the 3D car body, saved per car, and visible in lobby/race rendering.

The runtime target is the 100-car case. Customization must not turn into 16 dynamic decal nodes or 16 shader projector checks per car. The preferred race representation is one additional generated stamp mesh per unique livery/archetype, rendered with one material.

## Current constraints

- Car selection and player settings live in `mxto/player/player_settings.gd` and `mxto/ui/car_settings.gd`.
- Car assets are `CarDefinition` resources pointing to a scene with `VEHICLE_MAIN`, `VEHICLE_SHADOW`, `VEHICLE_OUTLINE`, `VEHICLE_OUTLINE_MAIN`, and `THRUSTERS`.
- Race/lobby bodies are batched by `mxto/vehicle/car_render_manager.gd` into `MultiMeshInstance3D` archetypes.
- Native render submission in `src/gamesim/gamesim.cpp` updates multimesh transforms/colours per visible car.
- Existing emote sticker resources under `mxto/ui/emote_sticker/` are transient HUD/social effects and should stay unrelated to body stamps.

## Saved data

Save livery data per car definition, not as one global player setting. A simple layout is:

- `user://garage_liveries/<safe_car_id>.json`
- `vehicle_content_id`
- `primary_colour`
- `secondary_colour`
- `accent_colour`
- optional `outline_colour` override
- optional `trail_colour` override
- `stamps`, max 16

Each stamp stores a local-space projector:

- `stamp_id`: asset/catalog id
- `enabled`
- `layer`: lower renders first
- `local_origin`: projector center in car local space
- `local_basis`: projector orientation in car local space
- `size`: projected width/height
- `projection_depth`: box depth along projector forward
- `colour`: modulation colour
- `opacity`

This format avoids UV dependence and survives mesh UV seams, mirrored UVs, and stretched islands.

## Garage editing flow

1. Instantiate the selected `CarDefinition.car_scene` in a garage preview world.
2. Raycast from the garage camera against the visible car body mesh.
3. Convert the hit point and normal into the car scene's local space.
4. Create a projector basis from the hit normal plus a stable tangent.
5. Let the player rotate, scale, recolour, reorder, duplicate, and delete the selected stamp.
6. Preview the selected stamp with a temporary Godot `Decal` or helper gizmo if convenient.
7. On save/apply, rebuild the generated livery render assets.

## Race render representation

Use one combined stamp mesh per unique livery/car archetype:

1. For each active stamp, transform candidate body triangles into projector space.
2. Reject triangles outside the projector box.
3. Clip remaining triangles against the projector box.
4. Emit decal vertices with position offset along the source normal, projected UVs from projector-space x/y, vertex colour from stamp modulation/opacity, and layer-preserving append order.
5. Combine all 16 stamps into one `ArrayMesh`.
6. Render that mesh with one atlas material, ideally as another `MultiMeshInstance3D` pass beside the body pass.

The renderer archetype key should include livery identity:

```text
vehicle_content_id + ":" + livery_hash
```

That keeps identical liveries batched. In the worst case of 100 unique liveries, there can be 100 extra stamp mesh draws, but there should not be 1600 decal draws or per-pixel loops over 16 projectors.

## Stamp assets

Create a separate catalog from emote stickers, for example:

- `mxto/vehicle/customization/stamps/`
- `mxto/vehicle/customization/stamp_catalog.tres`

The catalog should map stable `stamp_id` strings to rectangles in a stamp atlas. Runtime stamp meshes use atlas UVs and one material, not separate materials per stamp.

## Colour customization

Colour editing uses the vehicle body shader's `in_paint_mask` texture. The shader treats the car's `in_albedo` as the grayscale/detail base, then recolours masked pixels from the saved livery:

- Red channel: primary paint coverage
- Green channel: secondary paint coverage
- Blue channel: accent paint coverage
- Black/no mask: leave the original `in_albedo` colour unchanged

Author machine albedo textures as grayscale or close to grayscale when they should be player-tinted. Use the albedo value for baked panel detail, dirt, highlights, and shadows. Put paint-region selection in the mask texture, not in albedo brightness.

For a fully primary body panel, the mask should be red `(1, 0, 0)`. For a secondary stripe, use green `(0, 1, 0)`. For accent trim, use blue `(0, 0, 1)`. Soft masks and blends are allowed; overlapping channels are normalized before colour selection, while the summed mask controls how strongly the painted result replaces the grayscale albedo.

Existing materials without `in_paint_mask` fall back to using `in_albedo` as a mask when a livery is applied. That is only a compatibility path; new machine assets should set an explicit `shader_parameter/in_paint_mask`.

Outline and motion-trail colours retain the vehicle material's authored defaults until the player changes them in the garage. This keeps existing liveries and older replay payloads visually unchanged while allowing each colour to be customized independently.

## Implementation phases

1. Add livery data resources and JSON serialization.
2. Add a dedicated garage scene with car selection, colour controls, stamp catalog, layer list, and 3D placement gizmo.
3. Add editor-time/runtime livery asset generation for one combined stamp mesh.
4. Extend `CarRenderManager` archetypes with an optional stamp pass keyed by livery hash.
5. Pass per-racer livery ids through player settings/network settings.
6. Add paint masks and per-car material support.
7. Add smoke tests for serialization, renderer archetype grouping, and generated stamp mesh bounds. The initial generated mesh bound check lives at `mxto/test/car_livery_stamp_mesh_smoke.gd`.

## Performance rules

- No per-car stamp `Decal` nodes in races.
- No shader loop over all 16 stamp projectors in the main vehicle shader.
- No per-frame stamp mesh rebuilds.
- Rebuild livery assets only when the garage data changes or when a livery is first loaded.
- Keep stamp draw state to one atlas material per stamp pass.
- Pool/cached generated meshes by livery hash.
