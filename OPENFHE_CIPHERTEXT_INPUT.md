# OpenFHE complete ciphertext input flow

This flow keeps the original synthetic regressions unchanged.  It adds a
separate OpenFHE v1.5.1 exporter and HLS testbench for the complete raw CKKS
ciphertext multiplication:

```text
(lhs c0,c1) x (rhs c0,c1) -> (d0,d1,d2)
```

The binary vector contains every active RNS tower in OpenFHE EVALUATION/NTT
format.  It contains no plaintext, secret key, or decryption key.

## 1. Build the exporter in WSL/Linux

Use the OpenFHE installation already used by the legacy exporter.  From the
repository root:

```bash
cmake -S openfhe_host/ckks_mult \
  -B openfhe_host/build_ckks_mult \
  -DCMAKE_BUILD_TYPE=Release \
  -DOpenFHE_DIR=/path/to/openfhe-install/lib/OpenFHE
cmake --build openfhe_host/build_ckks_mult --parallel
```

## 2. Generate randomized real ciphertext inputs

```bash
LD_LIBRARY_PATH=/path/to/openfhe-install/lib \
  ./openfhe_host/build_ckks_mult/openfhe_export_ckks_mult \
  --output vectors/openfhe_ciphertext_mult.bin
```

The command writes a binary vector plus a human-readable JSON sidecar.  Each
run generates fresh randomized ciphertext residues.

## 3. Run HLS C simulation, synthesis, and C/RTL co-simulation

From a Windows Vitis Command Prompt at `ckks_ciphertext_mult`:

```bat
run_openfhe_vector_hls.bat ..\vectors\openfhe_ciphertext_mult.bin
```

The passing testbench ends with a line like:

```text
PASS: OpenFHE complete ciphertext multiply for 3 towers x 8192 residues
```

The exact tower count is selected by the OpenFHE crypto context and may change
if those parameters change.  Every FPGA output residue is compared bit for
bit with OpenFHE `EvalMultNoRelinNoCheck`.
