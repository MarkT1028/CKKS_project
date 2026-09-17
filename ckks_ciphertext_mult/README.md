# CKKS ciphertext-multiply accelerator

## Security boundary

The trusted PC runs OpenFHE KeyGen, Encode, Encrypt, and final Decrypt.  The
VEK280 never receives a plaintext, the secret key, or a decryption key.  It
receives two CKKS ciphertexts, public RNS parameters, and produces another
ciphertext.

The public evaluation/relinearization key is allowed on an untrusted compute
device, but this first accelerator deliberately stops before relinearization.
Its output is therefore a valid three-component CKKS ciphertext.

## Mathematical operation

For input ciphertexts `A=(a0,a1)` and `B=(b0,b1)`, and for every active RNS
tower `q[t]`, the kernel computes:

```
d0 = a0 * b0 mod q[t]
d1 = a0 * b1 + a1 * b0 mod q[t]
d2 = a1 * b1 mod q[t]
```

This is the complete component expansion of CKKS `EvalMultNoRelin`.  It must be
applied to DCRT polynomials already in OpenFHE `EVALUATION` (NTT) format.  It is
not a coefficient-domain polynomial convolution.

## Memory layout

All arrays are tower-major.  The linear index is:

```
index = tower * 8192 + coefficient
```

`lhs[index]` and `rhs[index]` are 128-bit words:

```
[127:64] c1 residue
[ 63: 0] c0 residue
```

`out[index]` is a 256-bit word:

```
[255:192] zero/reserved
[191:128] d2 residue
[127: 64] d1 residue
[ 63:  0] d0 residue
```

`rns_params[tower]` is a public 256-bit word:

```
[255:145] zero/reserved
[144: 64] floor(2^128 / q[tower])
[ 63:  0] q[tower]
```

The current arithmetic supports `2^48 <= q < 2^60`, up to 16 towers, and the
fixed CKKS ring dimension 8192.  Every input residue must already be reduced to
`[0,q)`.

## What this version does and does not do

It does:

- consume both components of each input ciphertext;
- process every active RNS tower supplied by the host;
- produce all three pre-relinearization output components;
- use four independent modular products per residue;
- expose only four AXI master bundles (`gmem0` through `gmem3`);
- pipeline the coefficient loop with a requested initiation interval of one.

It does not yet:

- perform relinearization/key switching;
- rescale or modulus-switch the result;
- perform NTT or inverse NTT;
- serialize OpenFHE objects by itself;
- decrypt or decode anything on the board.

The PC-side importer must restore the returned `d0,d1,d2` into a ciphertext
with the correct OpenFHE context, key tag, level, scale, noise-scale degree, and
slot count before decryption.  Relinearization and rescaling can initially be
done on the PC after timing the FPGA kernel; later accelerators can move those
ciphertext-only stages to the VEK280 as well.

## Files

- `src/ckks_ciphertext_mult.hpp`: interface, data layout, limits, status codes.
- `src/ckks_ciphertext_mult.cpp`: synthesizable HLS kernel.
- `tb/test_ckks_ciphertext_mult.cpp`: four-tower arithmetic C/RTL regression.
- `run_hls.tcl`: fresh Vitis HLS C sim, synthesis, RTL co-sim,
  and IP-export flow for the VEK280.

## Verified HLS result

Vitis HLS 2025.2 was run for `xcve2802-vsvh1760-2MP-e-S` with a 3.2 ns
target clock.  The bit-exact C simulation and Verilog C/RTL co-simulation both
pass for 8192 residues in each of four towers.  The regression covers 48-bit,
54-bit, 55-bit, and nearly 60-bit public moduli.

- coefficient-loop initiation interval: 1
- pipeline depth: 19 cycles
- one-tower pipeline latency: 8211 cycles / 26.275 us
- maximum 16-tower top-level estimate: 131967 cycles / 0.422 ms
- estimated Fmax: 438.80 MHz
- resources: 176 DSP, 32 BRAM_18K, 14466 FF, 12412 LUT, 0 URAM
- AXI data widths: 128-bit lhs, 128-bit rhs, 256-bit output, 256-bit parameters

These are HLS estimates and RTL co-simulation results, not post-route or
board-level measurements.  LPDDR/NoC placement and software cache management
will affect the final end-to-end latency.
