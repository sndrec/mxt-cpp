# Generated auth-input dictionaries

These headers contain static zstd dictionaries used by `NetcodeSession` to
evaluate and encode authoritative input packets. They are data artifacts, not
hand-authored netcode.

Use `scripts/train_auth_input_dict.py` to train and compare a dictionary from a
directory of captured `.bin` samples. The script reads
`auth_input_zstd_dictionary.h` as its default current dictionary and writes a
replacement only when `--write-header` is provided.

Keep generated dictionaries in this directory so the netcode implementation
and its data are easy to distinguish.
