# CKKS_project

VEK280 HLS development for OpenFHE CKKS ciphertext multiplication.

## Repository layout

- `ckks_rns_mac/`: Vitis HLS kernel and bit-exact testbench
- `openfhe_host/`: OpenFHE v1.5.1 C++ ciphertext-tower exporter
- `common/`: versioned host/HLS-testbench vector file format
- `vectors/`: generated local test-vector destination
- `ckks_ciphertext_mult/src/`: current fused ciphertext-multiply kernel
- `ckks_ciphertext_mult/tb/`: multi-tower bit-exact testbench
- `ckks_ciphertext_mult/README.md`: security boundary, layout, and results

The FPGA kernels do not link OpenFHE.  OpenFHE stays on the trusted PC; the
VEK280 datapath receives only canonical ciphertext residues and public RNS
parameters.  The existing legacy exporter still produces this earlier
one-tower MAC vector:

```text
out[i] = (acc[i] + a[i] * b[i]) mod q, i=0..8191
```

See `openfhe_host/README.md` for ciphertext export and
`ckks_rns_mac/README.md` for HLS simulation.

## Current ciphertext-multiply milestone

The current top function is `ckks_ciphertext_mult`.  For fresh two-component
evaluation-format ciphertexts `A=(a0,a1)` and `B=(b0,b1)`, it processes all
active towers supplied by the host and emits the complete three-component raw
product:

```text
d0 = a0*b0 mod q
d1 = a0*b1 + a1*b0 mod q
d2 = a1*b1 mod q
```

Inputs are packed as one 128-bit `{c1,c0}` word per tower/coefficient and the
output is one 256-bit `{reserved,d2,d1,d0}` word.  The core supports ring
dimension 8192, up to 16 towers, and public moduli in `[2^48,2^60)`.

The trusted PC owns OpenFHE KeyGen, Encode, Encrypt, and final Decrypt.  The
VEK280 receives only ciphertext residues and public RNS parameters.  The
secret key never enters the board.

Run the complete HLS regression with:

```powershell
cd ckks_ciphertext_mult
E:\AMDDesignTools\2025.2\Vitis\bin\vitis-run.bat --mode hls --tcl run_hls.tcl
```

The legacy one-tower proof-of-concept remains under `ckks_rns_mac/` as a
known-good arithmetic baseline.

## Legacy one-tower milestone

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
