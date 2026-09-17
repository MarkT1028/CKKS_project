# VEK280 OpenFHE CKKS remote board test

This directory contains the Cortex-A72 application and the fixed OpenFHE
ciphertext fixture used to test the `ckks_ciphertext_mult` IP on a VEK280.

## Files

- `deploy/ckks_ciphertext_mult_test.elf`: prebuilt Vitis 2025.2 standalone
  A72 application to download with XSCT.
- `main.c`: source for that application.
- `generated/openfhe_ciphertext_mult_data.h`: three-tower OpenFHE 1.5.1
  ciphertext inputs, public RNS parameters, and expected `d0/d1/d2`.
- `SHA256SUMS.txt`: hashes tying the deploy ELF to its source and fixture.

The fixture contains ciphertext residues and public RNS parameters only.  It
contains no plaintext, secret key, decryption key, or decoded values.

## Required PDI

This update changes only the A72 application.  Continue using the already
validated custom PDI containing the `ckks_ciphertext_mult` IP.  Reprogram
that PDI only after a power cycle, device reset, or a different PDI load.

## XSCT

With the custom PDI already programmed:

```tcl
targets -set -filter {name =~ "Cortex-A72 #0"}
stop
rst -processor -clear-registers
dow {ckks_ciphertext_mult_test.elf}
con
```

Use the complete local path after copying the ELF to the remote PC.  For
example:

```tcl
dow {C:/Users/ntutuser-3848/Desktop/114c75007/vek280_data/ckks_rms/deploy/ckks_ciphertext_mult_test.elf}
```

## Expected COM4 output

```text
VEK280 OpenFHE CKKS ciphertext multiplication test
Input: OpenFHE 1.5.1 encrypted ciphertext residues
Vector SHA-256: 27f5424d97602774ef1ec292dd487487c516f40c8d2d4710eaf273ce6389ff16
Loading 3 towers x 8192 OpenFHE residues...
FPGA kernel completed
PASS: FPGA output matches OpenFHE EvalMultNoRelinNoCheck
Validated 3 towers x 8192 residues
Output components: d0, d1, d2
checksum=0x202796E0A0AB29BF
```

Any `FAIL`, mismatch line, different checksum, or different vector SHA means
the board did not run this exact validated fixture.
