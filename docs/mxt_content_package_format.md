# MaxX Throttle Content Package Format Revision 1

This document freezes the first public package directory layout and both content digest algorithms. The same validator is used for local packages, manually shared `.mxtpkg` archives, and Steam Workshop installs.

## Canonical directory

A package is canonically an unpacked directory. Revision 1 uses fixed ASCII paths rather than creator-selected payload filenames. This is stricter than accepting and normalizing arbitrary relative paths: alternate separators, absolute paths, traversal segments, URI paths, case variants, device names, and duplicate spellings cannot appear in a valid package.

Vehicle package:

```text
manifest.json
preview.png
vehicle/
  model.glb
  properties.mxt_car_props
  visual.json
```

Track package:

```text
manifest.json
preview.png
track/
  track.mxt_track
  visual.glb
  metadata.json
```

Every entry must be a regular file or one of the single type-specific directories shown above. Symlinks, hidden entries, additional directories, undeclared files, and duplicate case-folded paths are rejected. `manifest.json` and every declared payload file are immutable inputs to the package digest.

## Manifest

`manifest.json` is UTF-8 JSON no larger than 64 KiB. Duplicate object members and unrecognized members are errors. Revision 1 has this exact root schema:

```json
{
  "format_revision": 1,
  "content_type": "vehicle",
  "title": "Example Machine",
  "description": "Creator-authored description",
  "author_name": "Example Creator",
  "payload": {
    "model": "vehicle/model.glb",
    "properties": "vehicle/properties.mxt_car_props",
    "visual_metadata": "vehicle/visual.json"
  },
  "payload_sha256": {
    "vehicle/model.glb": "64 lowercase hexadecimal characters",
    "vehicle/properties.mxt_car_props": "64 lowercase hexadecimal characters",
    "vehicle/visual.json": "64 lowercase hexadecimal characters",
    "preview.png": "64 lowercase hexadecimal characters"
  }
}
```

For `content_type: "track"`, `payload` is exactly:

```json
{
  "track": "track/track.mxt_track",
  "visual": "track/visual.glb",
  "metadata": "track/metadata.json"
}
```

The track hash table contains those three paths and `preview.png`. `payload_sha256` does not include `manifest.json`, because the complete raw manifest is already an input to `package_digest`.

String limits are 128 Unicode code points for `title`, 8,000 for `description`, and 64 for `author_name`. Title and author must be non-empty; description may be empty. Control characters are rejected.

## Vehicle visual metadata

`vehicle/visual.json` is strict data interpreted only by MaxX Throttle's renderer. It cannot name resources or provide shaders. Revision 1 selects body surfaces and conventional embedded glTF texture inputs in addition to the model transform and zero to eight game-owned thruster instances. The optional paint mask is sourced from the selected glTF material's occlusion texture; its RGB channels map to primary, secondary, and accent livery colours.

```json
{
  "format_revision": 1,
  "model_transform": {
    "translation": [0.0, 0.0, 0.0],
    "rotation_degrees": [0.0, 0.0, 0.0],
    "scale": [1.0, 1.0, 1.0]
  },
  "body_surfaces": [0],
  "material_inputs": {
    "albedo_surface": 0,
    "normal_surface": 0,
    "paint_mask_surface": -1
  },
  "thrusters": [
    {
      "position": [0.0, 0.25, -1.5],
      "rotation_degrees": [0.0, 0.0, 0.0],
      "scale": 0.5
    }
  ]
}
```

At least one unique body-surface index is required. Each material input is either `-1` for the game-owned flat fallback or a surface whose imported glTF material contains the corresponding conventional texture. All components must be finite. Model translation is limited to +/-1000 units, rotation to +/-3600 degrees, and positive scale to 0.001 through 100. Thruster position is limited to +/-100 units, rotation to +/-3600 degrees, and scale to 0.01 through 10. Unknown fields are rejected.

## Track metadata

`track/metadata.json` is a strict object containing exactly these revision 1 fields:

```json
{
  "difficulty": 1,
  "fog_distance": 2000.0,
  "sky_top_color": [0.1, 0.1, 0.2],
  "sky_horizon_color": [0.5, 0.5, 0.6],
  "sky_ground_color": [0.05, 0.05, 0.1],
  "ground_color": [0.1, 0.1, 0.1],
  "ground_height": -500.0,
  "cloud_color": [0.8, 0.8, 0.8],
  "cloud_height": 800.0,
  "light_color": [1.0, 0.95, 0.9],
  "light_intensity": 1.0,
  "ambient_intensity": 0.1,
  "ambient_color": [0.15, 0.15, 0.18],
  "light_direction": [0.3, -1.0, 0.4]
}
```

Colors contain three finite values from 0 through 1. `light_direction` contains three finite values from -1 through 1 and may not be the zero vector. Difficulty is an integer from 0 through 10. Fog distance, heights, and light intensities have bounded numeric ranges enforced by the native validator. Resource paths, songs, visual-scene overrides, segment data, and unknown fields are not accepted in community metadata.

## Digest encoding

All integer fields in digest input are unsigned and big-endian. Strings are UTF-8 without a terminator. Paths are sorted by their UTF-8 byte sequences. SHA-256 output is lowercase hexadecimal prefixed by `sha256:`.

`package_digest` is SHA-256 over:

```text
bytes("MXT_PACKAGE\0")
u32(package_format_revision)
for each accepted file, including manifest.json, in sorted path order:
  u32(path_byte_length)
  path_bytes
  u64(file_byte_length)
  file_bytes
```

`gameplay_digest` is SHA-256 over:

```text
bytes("MXT_GAMEPLAY\0")
u32(gameplay_digest_revision)
u32(content_type_byte_length)
content_type_bytes
u32(authoritative_path_byte_length)
authoritative_path_bytes
u64(authoritative_file_byte_length)
authoritative_file_bytes
```

Both revision fields are currently `1`. The authoritative file is `vehicle/properties.mxt_car_props` for a vehicle and `track/track.mxt_track` for a track. Presentation edits therefore change the package digest without changing gameplay identity.

## Catalog content IDs

Paths are locators, never identities. The native catalog assigns colon-delimited stable IDs:

```text
mxt:<vehicle|track>:package:<package-digest-hex>
mxt:<vehicle|track>:workshop:<published-file-id>
mxt:<vehicle|track>:official:<checked-in-slug>
```

A local package ID names one immutable package revision. A Workshop ID names the logical mutable listing and resolves to the exact currently validated package record. An official ID names a trusted asset shipped with the game; official records have a gameplay digest but no package digest because their resources are not distributed as `.mxtpkg` archives. Race admission and replay headers must therefore store the content ID and gameplay digest, plus the package digest whenever the source is a package. A Workshop ID by itself is never exact gameplay evidence.

Catalog records expose validated absolute paths for the preview, visual payload, authoritative gameplay payload, and track metadata. Settings, replay data, multiplayer negotiation, and selection UI consume the content ID and resolved record rather than persisting those paths.

Official vehicles declare their IDs in their `CarDefinition` resources. Official tracks are declared in `res://track/official_tracks.json`, which binds each checked-in slug to its shipped directory/files and expected gameplay digest. Existing trusted tracks are hashed but are not subjected to the stricter hostile-package structural validator; community and Workshop tracks always are. Changing an official track's authoritative bytes therefore requires an intentional manifest digest update.

Replay schema 4 and debug-replay schema 2 store `track_content_id` plus `track_gameplay_digest`, and store `vehicle_content_id` plus `vehicle_gameplay_digest` in every racer setting. Package digests and Workshop IDs are also required when the resolved source has them. Track paths, display names, and array positions are not identity fallbacks. Multiplayer race options carry parallel exact-digest/package/Workshop arrays for every advertised track, while player settings carry the same evidence for vehicles.

## Initial limits and accepted payloads

- Vehicle package: 64 MiB total; model 48 MiB; properties 4 MiB; visual metadata 64 KiB.
- Track package: 512 MiB total; collision payload 256 MiB; visual GLB 256 MiB; metadata 1 MiB.
- Preview: PNG, 8 MiB, maximum 4096 by 4096 pixels.
- At most eight files, including `manifest.json`.
- Vehicle visual: at most 250,000 triangles.
- Track visual: at most 2,000,000 triangles.
- Embedded GLB textures: PNG or JPEG, maximum 4096 by 4096, with type-specific aggregate pixel budgets.

GLB 2.0 is required. Revision 1 accepts static triangle meshes with embedded buffers and embedded images. It rejects external/data URIs, unknown or required extensions, animations, skins, cameras, morph targets, unsupported vertex attributes, malformed accessors, over-deep/cyclic node hierarchies, and resource counts over the native budgets. The non-required `GODOT_single_root` marker emitted by Godot's canonical GLB writer is the sole accepted extension name. After structural and resource-budget validation, Godot's GLB parser must also accept the document before the package is valid.

Vehicle properties must pass the native `.mxt_car_props` schema, CRC, curve, and required-stat validator. Track gameplay data must be the current `v0.9` `.mxt_track` format and pass complete bounded parsing, index checks, finite-number checks, strictly ordered curve checks, and collision-triangle checks before the runtime loader can receive it.

## `.mxtpkg`

A `.mxtpkg` is a ZIP32 transport for exactly one canonical package directory. It contains the exact package files plus one empty `vehicle/` or `track/` directory entry. Entry names use the canonical ASCII paths above.

Revision 1 archives:

- contain no preamble, comment, trailing data, hidden local records, extra fields, ZIP64 records, or multi-disk data;
- contain no encryption, data descriptors, symlinks, or unsupported compression methods;
- use stored or Deflate compression and contain matching local/central headers;
- contain at most eight entries and no duplicate case-folded paths;
- obey the package and per-file uncompressed limits before any entry is extracted;
- reject entries larger than 1 MiB whose declared compression ratio exceeds 200:1; and
- must contain exactly the files declared by the successfully parsed manifest.

Import first preflights the raw central directory and local headers, then decompresses into a private staging directory, applies the full package validator, and atomically installs the result under its package-digest hex string. Reimporting identical bytes reuses the valid content-addressed installation. Export validates the source directory, writes sorted canonical paths to a temporary archive, preflights that archive, and only then moves it to the requested output path.

The unpacked directory and its digests remain authoritative. ZIP compression, timestamps, and other container metadata are not part of either digest.
