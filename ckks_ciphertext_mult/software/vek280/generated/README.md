# Generated OpenFHE ciphertext data

Generate the board-side C header from the repository root with:

```powershell
python tools/ckks_ct_vector_to_c_header.py `
  vectors/openfhe_ciphertext_mult.bin `
  ckks_ciphertext_mult/software/vek280/generated/openfhe_ciphertext_mult_data.h
```

The repository tracks `openfhe_ciphertext_mult_data.h` as the canonical
board-test fixture.  It is the exact input embedded in the prebuilt ELF under
`../deploy/`; its source-vector SHA-256 is recorded inside the header.

OpenFHE encryption is randomized, so regenerating the binary vector and this
header intentionally creates a new fixture.  Rebuild and replace the deploy
ELF whenever the tracked header changes.
