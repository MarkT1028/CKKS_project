# CKKS RNS modular MAC HLS proof-of-concept

This directory is intentionally isolated from the existing Python/OpenFHE
pipeline. It extracts one fixed-size arithmetic primitive used beneath the
expensive `g_a.3` ciphertext-by-ciphertext operations:

```text
out[i] = (acc[i] + a[i] * b[i]) mod q,  i = 0..8191
```

Parameters match the project configuration:

- CKKS ring dimension: 8192
- RNS limb width: 55 bits
- run-time selectable 55-bit modulus `q`
- run-time Barrett constant `mu = floor(2^110/q)`
- VEK280 part: `xcve2802-vsvh1760-2MP-e-S`
- synthesis clock target: 312.5 MHz (3.2 ns)

The host must supply the exact modulus of the selected OpenFHE tower. The
kernel uses generic division-free Barrett reduction, so one bitstream can
process different 55-bit towers. Inputs must be canonical residues in
`[0,q)` and must be in OpenFHE `EVALUATION`/NTT format.

## Scope

This is a bit-exact HLS micro-kernel for one evaluation-format RNS tower. It
does **not** yet implement a complete OpenFHE `EvalMult`; the following remain
outside this first proof-of-concept:

- multiple active RNS towers
- ciphertext component expansion (`c0/c1/c2`)
- relinearization and key switching
- rescaling/modulus drop
- OpenFHE object reconstruction after FPGA execution

## Real OpenFHE ciphertext input

The repository now contains a host-side OpenFHE v1.5.1 C++ exporter under
`../openfhe_host`. It writes one selected `c0` tower and the matching raw
OpenFHE `c0*c0` result to a versioned binary vector file. After generating a
file, run:

```bat
run_vector_csim.bat ..\vectors\openfhe_c0xc0_t55.bin
run_vector_cosim.bat ..\vectors\openfhe_c0xc0_t55.bin
```

Without `CKKS_VECTOR_FILE`, the testbench keeps running deterministic random
regressions. OpenFHE is never included in the synthesizable kernel.

## Run

From this directory:

```powershell
.\run_hls.ps1
```

Because the local PowerShell execution policy blocks unsigned `.ps1` files,
the simplest complete command on this machine is:

```bat
run_all.bat
```

It runs bit-exact C simulation, C synthesis, C/RTL co-simulation, and Vivado
out-of-context synthesis/place/route. Reports are written under:

```text
build/solution1/syn/report/
vivado_ooc/reports/
```

IP packaging is deliberately separate from the default flow because this
machine's user Tcl Store currently reports a corrupted app initializer. The
generated Verilog and Vivado OOC design do not depend on that Tcl Store.

## Measured results on this machine

Toolchain: Vitis/Vivado 2025.2, target
`xcve2802-vsvh1760-2MP-e-S`, 3.2 ns clock constraint.

| Check | Result |
|---|---:|
| Deterministic C simulation, two 55-bit moduli | PASS, 8192/8192 for each modulus |
| OpenFHE v1.5.1 ciphertext-tower C simulation | PASS, 8192/8192 bit-exact |
| OpenFHE ciphertext-tower Verilog C/RTL co-simulation | PASS, 8192/8192 bit-exact |
| HLS achieved loop II | 1 |
| HLS pipeline depth | 41 cycles |
| HLS estimated kernel latency | 8233 cycles / 26.346 us |
| OpenFHE-vector RTL co-sim transaction latency | 8293 cycles |
| HLS estimated Fmax | about 456.8 MHz |
| BRAM_18K | 14 / 1200 |
| DSP | 27 / 1312 |
| FF | 7296 / 1041408 |
| LUT | 5611 / 520704 |
| URAM | 0 / 264 |

These measurements describe the new generic run-time-modulus Barrett datapath.
Compared with the backed-up fixed-q reducer it spends more DSPs because the
modulus is no longer compiled into the circuit. The four 64-bit AXI ports imply
a peak streaming demand of 32 bytes/cycle (three reads and one write), so
board-level performance will depend on memory-port placement and whether
operands remain resident across multiple CKKS operations.

Vivado out-of-context synthesis currently stops at license checkout:

```text
A valid license was not found for feature 'Synthesis' and/or device 'xcve2802'.
```

No `XILINXD_LICENSE_FILE` or `LM_LICENSE_FILE` is configured and no user
`.lic` file was found. After activating a license that covers XCVE2802, rerun:

```bat
E:\AMDDesignTools\2025.2\Vivado\bin\vivado.bat ^
  -mode batch -nojournal -nolog -source run_vivado_ooc.tcl
```
