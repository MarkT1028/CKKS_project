#!/usr/bin/env python3
"""Convert a CKKSCTM2 binary vector into a self-contained C header.

The generated header contains only public RNS parameters, ciphertext residues,
and OpenFHE golden output residues.  It never contains plaintext, a secret key,
or a decryption key.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence


MAGIC = b"CKKSCTM2"
VERSION = 2
HEADER_BYTES = 64
TOWER_RECORD_BYTES = 32
WORD_BITS = 64
EVALUATION_DOMAIN = 1
INPUT_COMPONENTS = 2
OUTPUT_COMPONENTS = 3
PAYLOAD_ARRAY_COUNT = 7
MAXIMUM_TOWERS = 16
HEADER_STRUCT = struct.Struct("<8s12IQ")
TOWER_STRUCT = struct.Struct("<4Q")
PAYLOAD_NAMES = (
    "lhs_c0",
    "lhs_c1",
    "rhs_c0",
    "rhs_c1",
    "expected_d0",
    "expected_d1",
    "expected_d2",
)


@dataclass(frozen=True)
class TowerParameter:
    modulus: int
    reciprocal_low: int
    reciprocal_high: int


@dataclass(frozen=True)
class CiphertextVector:
    ring_dim: int
    tower_count: int
    towers: tuple[TowerParameter, ...]
    payloads: tuple[tuple[int, ...], ...]
    source_sha256: str

    @property
    def word_count(self) -> int:
        return self.ring_dim * self.tower_count


def _read_u64_array(data: bytes, offset: int, count: int) -> tuple[int, ...]:
    byte_count = count * 8
    end = offset + byte_count
    if end > len(data):
        raise ValueError("unexpected end of ciphertext payload")
    return tuple(value[0] for value in struct.iter_unpack("<Q", data[offset:end]))


def read_vector(path: Path) -> CiphertextVector:
    data = path.read_bytes()
    if len(data) < HEADER_BYTES:
        raise ValueError("ciphertext vector is smaller than its fixed header")

    fields = HEADER_STRUCT.unpack_from(data, 0)
    (
        magic,
        version,
        header_bytes,
        ring_dim,
        tower_count,
        word_bits,
        domain,
        input_components,
        output_components,
        flags,
        reserved_u32,
        payload_array_count,
        tower_record_bytes,
        reserved_u64,
    ) = fields

    if magic != MAGIC:
        raise ValueError(f"bad magic {magic!r}; expected {MAGIC!r}")
    if version != VERSION or header_bytes != HEADER_BYTES:
        raise ValueError("unsupported ciphertext vector version/header size")
    if ring_dim == 0 or tower_count == 0 or tower_count > MAXIMUM_TOWERS:
        raise ValueError("invalid ring dimension or tower count")
    if word_bits != WORD_BITS or domain != EVALUATION_DOMAIN:
        raise ValueError("vector is not 64-bit EVALUATION/NTT data")
    if input_components != INPUT_COMPONENTS:
        raise ValueError("vector does not contain two input components")
    if output_components != OUTPUT_COMPONENTS:
        raise ValueError("vector does not contain three output components")
    if flags != 0 or reserved_u32 != 0 or reserved_u64 != 0:
        raise ValueError("unsupported nonzero header flags/reserved fields")
    if payload_array_count != PAYLOAD_ARRAY_COUNT:
        raise ValueError("unexpected ciphertext payload array count")
    if tower_record_bytes != TOWER_RECORD_BYTES:
        raise ValueError("unexpected RNS tower record size")

    word_count = ring_dim * tower_count
    expected_bytes = (
        HEADER_BYTES
        + tower_count * TOWER_RECORD_BYTES
        + PAYLOAD_ARRAY_COUNT * word_count * 8
    )
    if len(data) != expected_bytes:
        raise ValueError(
            f"file size mismatch: got {len(data)}, expected {expected_bytes}"
        )

    towers: list[TowerParameter] = []
    offset = HEADER_BYTES
    for tower_index in range(tower_count):
        modulus, reciprocal_low, reciprocal_high, reserved = (
            TOWER_STRUCT.unpack_from(data, offset)
        )
        offset += TOWER_RECORD_BYTES
        if reserved != 0:
            raise ValueError(f"tower {tower_index} has nonzero reserved data")
        if not (1 << 48) <= modulus < (1 << 60):
            raise ValueError(f"tower {tower_index} modulus is outside [2^48,2^60)")
        reciprocal = (1 << 128) // modulus
        if reciprocal_low != reciprocal & ((1 << 64) - 1):
            raise ValueError(f"tower {tower_index} reciprocal_low is incorrect")
        if reciprocal_high != reciprocal >> 64:
            raise ValueError(f"tower {tower_index} reciprocal_high is incorrect")
        towers.append(
            TowerParameter(modulus, reciprocal_low, reciprocal_high)
        )

    payloads: list[tuple[int, ...]] = []
    for payload_name in PAYLOAD_NAMES:
        values = _read_u64_array(data, offset, word_count)
        offset += word_count * 8
        for tower_index, tower in enumerate(towers):
            base = tower_index * ring_dim
            end = base + ring_dim
            if any(value >= tower.modulus for value in values[base:end]):
                raise ValueError(
                    f"{payload_name} contains an out-of-range residue in "
                    f"tower {tower_index}"
                )
        payloads.append(values)

    if offset != len(data):
        raise ValueError("internal parser error: unconsumed ciphertext bytes")
    return CiphertextVector(
        ring_dim=ring_dim,
        tower_count=tower_count,
        towers=tuple(towers),
        payloads=tuple(payloads),
        source_sha256=hashlib.sha256(data).hexdigest(),
    )


def _guard_from_path(path: Path) -> str:
    stem = re.sub(r"[^A-Za-z0-9]", "_", path.name).upper()
    if not stem or stem[0].isdigit():
        stem = "CKKS_" + stem
    return stem + "_INCLUDED"


def _write_u64_array(
    output,
    c_name: str,
    values: Sequence[int],
    size_expression: str,
    values_per_line: int = 4,
) -> None:
    output.write(
        f"static const uint64_t {c_name}[{size_expression}] "
        "__attribute__((aligned(64))) = {\n"
    )
    for index in range(0, len(values), values_per_line):
        chunk = values[index : index + values_per_line]
        encoded = ", ".join(f"UINT64_C(0x{value:016X})" for value in chunk)
        suffix = "," if index + values_per_line < len(values) else ""
        output.write(f"    {encoded}{suffix}\n")
    output.write("};\n\n")


def write_header(path: Path, vector: CiphertextVector, source_path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    guard = _guard_from_path(path)
    with path.open("w", encoding="ascii", newline="\n") as output:
        output.write(
            "/* Auto-generated from a CKKSCTM2 OpenFHE ciphertext vector.\n"
            " * Do not edit by hand; regenerate with "
            "tools/ckks_ct_vector_to_c_header.py.\n"
            " * Contains ciphertext residues and public RNS parameters only.\n"
            " * Contains no plaintext, secret key, or decryption key.\n"
            f" * Source file: {source_path.name}\n"
            f" * Source SHA-256: {vector.source_sha256}\n"
            " */\n"
            f"#ifndef {guard}\n"
            f"#define {guard}\n\n"
            "#include <stdint.h>\n\n"
            f"#define CKKS_OPENFHE_RING_DIM {vector.ring_dim}U\n"
            f"#define CKKS_OPENFHE_TOWER_COUNT {vector.tower_count}U\n"
            "#define CKKS_OPENFHE_WORD_COUNT "
            "(CKKS_OPENFHE_RING_DIM * CKKS_OPENFHE_TOWER_COUNT)\n"
            f"#define CKKS_OPENFHE_VECTOR_SHA256 \"{vector.source_sha256}\"\n\n"
        )

        _write_u64_array(
            output,
            "ckks_openfhe_moduli",
            [tower.modulus for tower in vector.towers],
            "CKKS_OPENFHE_TOWER_COUNT",
        )
        _write_u64_array(
            output,
            "ckks_openfhe_reciprocal_low",
            [tower.reciprocal_low for tower in vector.towers],
            "CKKS_OPENFHE_TOWER_COUNT",
        )
        _write_u64_array(
            output,
            "ckks_openfhe_reciprocal_high",
            [tower.reciprocal_high for tower in vector.towers],
            "CKKS_OPENFHE_TOWER_COUNT",
        )
        for payload_name, values in zip(PAYLOAD_NAMES, vector.payloads):
            _write_u64_array(
                output,
                f"ckks_openfhe_{payload_name}",
                values,
                "CKKS_OPENFHE_WORD_COUNT",
            )
        output.write(f"#endif  /* {guard} */\n")


def expected_header_values(vector: CiphertextVector) -> Iterable[int]:
    yield from (tower.modulus for tower in vector.towers)
    yield from (tower.reciprocal_low for tower in vector.towers)
    yield from (tower.reciprocal_high for tower in vector.towers)
    for payload in vector.payloads:
        yield from payload


def verify_header(path: Path, vector: CiphertextVector) -> int:
    text = path.read_text(encoding="ascii")
    match = re.search(
        r'#define CKKS_OPENFHE_VECTOR_SHA256 "([0-9a-f]{64})"', text
    )
    if match is None or match.group(1) != vector.source_sha256:
        raise ValueError("generated header has the wrong source SHA-256")

    actual = [
        int(token, 16)
        for token in re.findall(r"UINT64_C\(0x([0-9A-F]{16})\)", text)
    ]
    expected = list(expected_header_values(vector))
    if len(actual) != len(expected):
        raise ValueError(
            f"generated header value count mismatch: "
            f"got {len(actual)}, expected {len(expected)}"
        )
    for index, (got, wanted) in enumerate(zip(actual, expected)):
        if got != wanted:
            raise ValueError(
                f"generated header mismatch at uint64 index {index}: "
                f"got 0x{got:016X}, expected 0x{wanted:016X}"
            )
    return len(actual)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert a complete OpenFHE CKKS ciphertext vector to C"
    )
    parser.add_argument("input", type=Path, help="input CKKSCTM2 .bin file")
    parser.add_argument("output", type=Path, help="output C header")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    vector = read_vector(args.input)
    write_header(args.output, vector, args.input)
    checked_values = verify_header(args.output, vector)
    print("CKKSCTM2 validation: PASS")
    print(f"  source: {args.input}")
    print(f"  source SHA-256: {vector.source_sha256}")
    print(f"  ring dimension: {vector.ring_dim}")
    print(f"  RNS towers: {vector.tower_count}")
    print(f"  residues per component: {vector.word_count}")
    print(f"  generated header: {args.output}")
    print(f"  header uint64 round-trip: PASS ({checked_values} values)")
    print("  plaintext/secret key serialized: no")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}")
        raise SystemExit(1)
