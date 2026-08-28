# Vehicle customization

This directory owns vehicle liveries, built-in body stamps, custom stamp data,
and the atlas inputs consumed by vehicle rendering. The livery editor and
multiplayer content paths use these types as their shared representation.

## Responsibilities

- `CarLivery` stores paint, outline, trail, and layered body-stamp choices for
  one vehicle content ID. A livery contains up to 16 `CarLiveryStamp` resources.
- `CarLiveryStore` persists liveries under `user://garage_liveries` and migrates
  legacy livery data associated with a vehicle definition.
- `CarStampCatalog` and `CarStampEntry` describe the built-in stamp atlas and
  create the stamp render material.
- `CustomStampBlob`, `CustomStampStore`, and `CustomStampPaletteCatalog` own
  imported or painted custom stamp pixels and their persistent local library.
- `CustomStampPacker` assigns custom stamp rectangles within a player's atlas
  region.
- `CustomStampAtlasBuilder` allocates player regions and assembles the shared
  2048 by 2048 custom stamp image used by vehicle render passes.

## Data model and persistence

`CarLivery` serializes a versioned dictionary containing the vehicle content
ID, five configurable colours, customization flags for outline and trail
colours, and stamps sorted by layer. Its livery hash identifies identical
rendering data, while its livery key combines that hash with the vehicle content
ID.

Each `CarLiveryStamp` records its catalog or custom source, layer, local-space
projector transform, size, flips, projection depth, colour, and opacity. Custom
stamps also carry their content hash, palette ID, and packed atlas rectangle.

`CarLiveryStore` writes one JSON document per safe vehicle content ID beneath
`user://garage_liveries`. `CustomStampStore` writes content-addressed stamp
documents and `library.json` beneath `user://custom_stamps`. Deleting a custom
stamp also removes its references from saved liveries.

## Built-in and custom stamp assets

`stamp_catalog.tres` references `stamp_atlas.png` and assigns stable stamp IDs
to atlas tiles. `CarStampCatalog` resolves those IDs for editor previews,
projected mesh UVs, and stamp materials.

Custom stamps use palette-indexed, Zstandard-compressed `CustomStampBlob`
resources identified by a SHA-256 content hash. The store imports PNG images,
validates dimensions and byte budgets, creates previews, and produces manifests
for rendering and multiplayer transfer. The livery editor also supports painted
4-bit custom palettes through the same blob format.

## Atlas and rendering boundary

`CustomStampPacker` deduplicates hashes referenced by a livery and assigns each
blob to a wide or tall player region. `CustomStampAtlasBuilder` combines those
regions into the race, lobby, or garage atlas. Native
`NativeCustomStampImageBuilder` code expands palette-indexed pixels into the
atlas image.

`CarRenderManager` groups vehicles by vehicle and livery identity. Its native
`NativeStampMeshBuilder` clips projected stamps to the vehicle body and creates
the stamp mesh plus visibility mask. The resulting stamp pass uses the built-in
catalog atlas and the shared custom atlas through one catalog material.

## Primary consumers

- `mxto/ui/livery_editor.gd` edits paint and stamp placement.
- `mxto/ui/custom_stamp_library_controller.gd` and
  `custom_stamp_painter_controller.gd` manage the local custom stamp library.
- `mxto/ui/livery_atlas_controller.gd` builds threaded garage previews and
  reports the current custom-stamp budgets.
- `mxto/netplay/custom_stamp_network_controller.gd` exchanges manifests and
  missing blobs between peers.
- `mxto/vehicle/vehicle_content_controller.gd` prepares racer-specific atlas
  records for races and other vehicle presentation.
- `mxto/vehicle/car_render_manager.gd` installs generated stamp meshes and atlas
  textures into batched vehicle archetypes.
- `mxto/ui/lobby_chibi_controller.gd` builds the corresponding lobby atlas.
