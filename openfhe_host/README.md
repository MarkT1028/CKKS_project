# OpenFHE CKKS RNS exporter

This host-side C++ program is intentionally separate from Vitis HLS. It uses
OpenFHE v1.5.1 to create two real CKKS ciphertexts, selects one 55-bit
evaluation-format RNS tower from component `c0`, and exports data for the HLS
kernel.

OpenFHE is **not** synthesized into the FPGA. The FPGA receives only canonical
`uint64_t` residues and public arithmetic metadata; the secret key remains on
the host.

## Build with OpenFHE v1.5.1

Use the same compiler/toolchain that was used to build/install OpenFHE. The
following flow was tested in Ubuntu under WSL on this Windows machine:

```bash
git clone --branch v1.5.1 --depth 1 \
  https://github.com/openfheorg/openfhe-development.git
cmake -S openfhe-development -B openfhe-build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/path/to/openfhe-install \
  -DBUILD_UNITTESTS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_BENCHMARKS=OFF
cmake --build openfhe-build --parallel
cmake --install openfhe-build

cmake -S /path/to/CKKS_project/openfhe_host \
  -B /path/to/CKKS_project/openfhe_host/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DOpenFHE_DIR=/path/to/openfhe-install/lib/OpenFHE
cmake --build /path/to/CKKS_project/openfhe_host/build --parallel
```

The exact `OpenFHE_DIR` is the directory containing
`OpenFHEConfig.cmake`. The CMake project requires version 1.5.1 exactly.

## Export one real ciphertext tower

Run from the repository root:

```bash
LD_LIBRARY_PATH=/path/to/openfhe-install/lib \
  ./openfhe_host/build/openfhe_export_ckks \
  --output vectors/openfhe_c0xc0_t55.bin
```

The exporter prints every available tower modulus and automatically chooses
the first 55-bit tower. Select a specific 55-bit tower with `--tower INDEX`.
It also writes a human-readable JSON sidecar next to the binary file.

The binary record contains a fixed 64-byte little-endian header followed by:

1. `a[8192]` = left ciphertext `c0` residues
2. `b[8192]` = right ciphertext `c0` residues
3. `acc[8192]` = zeros for the first milestone
4. `expected[8192]` = OpenFHE raw `EvalMultNoRelinNoCheck` `e0` residues

No serialized key, plaintext, or secret material is written.

The generated `.bin` and `.json` files are intentionally ignored by Git.
Encryption is randomized, so each export is expected to produce different
residues while still passing the same exact modular-arithmetic check.
