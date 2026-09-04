# CKKS_project

VEK280 proof-of-concept for accelerating the RNS modular arithmetic beneath
OpenFHE CKKS ciphertext multiplication.

## Repository layout

- `ckks_rns_mac/`: Vitis HLS kernel and bit-exact testbench
- `openfhe_host/`: OpenFHE v1.5.1 C++ ciphertext-tower exporter
- `common/`: versioned host/HLS-testbench vector file format
- `vectors/`: generated local test-vector destination

The FPGA kernel does not link OpenFHE. OpenFHE runs on the host, extracts one
55-bit evaluation-format DCRT tower, and writes canonical `uint64_t` residues.
The HLS kernel reads those residues and evaluates:

```text
out[i] = (acc[i] + a[i] * b[i]) mod q, i=0..8191
```

See `openfhe_host/README.md` for ciphertext export and
`ckks_rns_mac/README.md` for HLS simulation.

## Current milestone

The host-to-HLS path has been validated with OpenFHE v1.5.1:

1. OpenFHE creates and encrypts two CKKS packed plaintexts.
2. The C++ exporter selects ciphertext component `c0`, RNS tower 1, in
   `EVALUATION`/NTT format.
3. It exports 8192 residue pairs, the 55-bit tower modulus, and the matching
   Barrett constant.
4. The HLS kernel computes `c0_left[i] * c0_right[i] mod q`.
5. C simulation and Verilog C/RTL co-simulation both match OpenFHE for all
   8192 coefficients.

This milestone is real ciphertext-domain modular arithmetic, but only one
tower and one output component. It is not yet a complete CKKS ciphertext
multiplication with `c0/c1/c2`, every tower, relinearization, and rescaling.
