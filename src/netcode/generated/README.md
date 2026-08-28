# Generated auth-input dictionaries

These three generated headers are the dictionaries reachable from the live
authoritative-input encoder:

- `auth_input_zero_bitmap_zstd_dictionary.h` for the general zero-bitmap layout;
- `auth_input_zero_bitmap_strafe_sparse_zstd_dictionary.h` for two-frame sparse
  strafe packets;
- `auth_input_hybrid_smooth_zstd_dictionary.h` for smooth analog deltas.

They are data artifacts, not hand-authored netcode. Obsolete experimental
dictionaries are deliberately not retained.
