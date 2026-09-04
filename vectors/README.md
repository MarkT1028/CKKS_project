# Generated CKKS RNS vectors

This directory receives `.bin` and `.json` files from
`openfhe_host/openfhe_export_ckks`. Generated ciphertext vectors are ignored
by Git because encryption is randomized and each run produces different data.

To run HLS C simulation with an exported vector on Windows:

```bat
cd ckks_rns_mac
run_vector_csim.bat ..\vectors\openfhe_c0xc0_t55.bin
run_vector_cosim.bat ..\vectors\openfhe_c0xc0_t55.bin
```
