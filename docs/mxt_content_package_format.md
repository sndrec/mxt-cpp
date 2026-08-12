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
    "properties": "vehicle/properties.mxt_car_props"
  },
  "payload_sha256": {
    "vehicle/model.glb": "64 lowercase hexadecimal characters",
    "vehicle/properties.mxt_car_props": "64 lowercase hexadecimal characters",
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

## Initial limits and accepted payloads

- Vehicle package: 64 MiB total; model 48 MiB; properties 4 MiB.
- Track package: 512 MiB total; collision payload 256 MiB; visual GLB 256 MiB; metadata 1 MiB.
- Preview: PNG, 8 MiB, maximum 4096 by 4096 pixels.
- At most eight files, including `manifest.json`.
- Vehicle visual: at most 250,000 triangles.
- Track visual: at most 2,000,000 triangles.
- Embedded GLB textures: PNG or JPEG, maximum 4096 by 4096, with type-specific aggregate pixel budgets.

GLB 2.0 is required. Revision 1 accepts static triangle meshes with embedded buffers and embedded images. It rejects external/data URIs, extensions, animations, skins, cameras, morph targets, unsupported vertex attributes, malformed accessors, over-deep/cyclic node hierarchies, and resource counts over the native budgets. After structural and resource-budget validation, Godot's GLB parser must also accept the document before the package is valid.

Vehicle properties must pass the native `.mxt_car_props` schema, CRC, curve, and required-stat validator. Track gameplay data must be the current `v0.9` `.mxt_track` format and pass complete bounded parsing, index checks, finite-number checks, strictly ordered curve checks, and collision-triangle checks before the runtime loader can receive it.

## `.mxtpkg`

A `.mxtpkg` is a ZIP transport for exactly one canonical package directory. Archive import/export will be specified and implemented as a separate layer: archive limits and path validation must run before extraction, and successful extraction must reproduce the canonical directory byte-for-byte. The unpacked directory and its digests remain authoritative; ZIP container metadata is not part of either digest.
