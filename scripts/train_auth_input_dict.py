#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
from pathlib import Path

import zstandard as zstd


def _read_header_dict(path: Path) -> bytes:
    text = path.read_text(encoding="utf-8")
    return bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", text))


def _packet_size_plain(sample: bytes) -> int:
    return len(zstd.ZstdCompressor(level=1).compress(sample)) + 10


def _packet_size_dict(sample: bytes, dictionary: bytes) -> int:
    dict_data = zstd.ZstdCompressionDict(dictionary)
    return len(zstd.ZstdCompressor(level=1, dict_data=dict_data).compress(sample)) + 10


def _summarize(name: str, sizes: list[int]) -> str:
    if not sizes:
        return f"{name}: no samples"
    return (
        f"{name}: avg={sum(sizes) / len(sizes):.3f} "
        f"min={min(sizes)} max={max(sizes)}"
    )


def _write_header(path: Path, dictionary: bytes, note: str) -> None:
    lines = [
        "#pragma once",
        "",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        f"// Static zstd dictionary trained from {note}.",
        "static constexpr uint8_t MXT_AUTH_INPUT_ZSTD_DICT[] = {",
    ]
    for i in range(0, len(dictionary), 16):
        chunk = dictionary[i : i + 16]
        suffix = "," if i + 16 < len(dictionary) else ""
        lines.append("\t" + ", ".join(f"0x{b:02x}" for b in chunk) + suffix)
    lines.extend(
        [
            "};",
            "static constexpr size_t MXT_AUTH_INPUT_ZSTD_DICT_SIZE = sizeof(MXT_AUTH_INPUT_ZSTD_DICT);",
            "",
        ]
    )
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--samples", required=True)
    parser.add_argument("--current-header", default="src/mxt_core/auth_input_zstd_dictionary.h")
    parser.add_argument("--write-header", default="")
    parser.add_argument("--dict-size", type=int, default=4096)
    parser.add_argument("--train-split", type=float, default=0.75)
    args = parser.parse_args()

    sample_dir = Path(args.samples)
    samples = [p.read_bytes() for p in sorted(sample_dir.glob("*.bin")) if p.stat().st_size > 0]
    if len(samples) < 8:
        raise SystemExit(f"need at least 8 samples, got {len(samples)}")

    split = max(1, min(len(samples) - 1, int(len(samples) * args.train_split)))
    train_samples = samples[:split]
    test_samples = samples[split:]
    trained_dict = zstd.train_dictionary(args.dict_size, train_samples)
    trained = trained_dict.as_bytes()
    current = _read_header_dict(Path(args.current_header))

    print(f"samples={len(samples)} train={len(train_samples)} test={len(test_samples)} sample_size={len(samples[0])}")
    for label, group in (("train", train_samples), ("test", test_samples), ("all", samples)):
        plain = [_packet_size_plain(s) for s in group]
        current_sizes = [_packet_size_dict(s, current) for s in group]
        trained_sizes = [_packet_size_dict(s, trained) for s in group]
        print(_summarize(f"{label} plain", plain))
        print(_summarize(f"{label} current_dict", current_sizes))
        print(_summarize(f"{label} trained_dict_{args.dict_size}", trained_sizes))

    if args.write_header:
        _write_header(
            Path(args.write_header),
            trained,
            f"{len(train_samples)} auth input samples from {sample_dir.name}",
        )
        print(f"wrote {args.write_header} bytes={len(trained)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
