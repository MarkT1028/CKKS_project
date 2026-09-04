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
- representative NTT-friendly prime:
  `q = 36028797018652673 = 2^55 - 311295`
- VEK280 part: `xcve2802-vsvh1760-2MP-e-S`
- synthesis clock target: 312.5 MHz (3.2 ns)

The prime satisfies `(q - 1) % (2 * 8192) == 0`. Its pseudo-Mersenne form
allows division-free reduction using two folds.

## Scope

This is a bit-exact HLS micro-kernel for one evaluation-format RNS tower. It
does **not** yet implement a complete OpenFHE `EvalMult`; the following remain
outside this first proof-of-concept:

- multiple active RNS towers
- ciphertext component expansion (`c0/c1/c2`)
- relinearization and key switching
- rescaling/modulus drop
- OpenFHE object marshaling

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
| C simulation | PASS, 8192/8192 bit-exact |
| Verilog C/RTL co-simulation | PASS, 8192/8192 bit-exact |
| HLS achieved loop II | 1 |
| HLS pipeline depth | 34 cycles |
| HLS estimated kernel latency | 8226 cycles / 26.323 us |
| RTL co-sim transaction latency | 8286 cycles / 26.515 us |
| HLS estimated Fmax | 503.52 MHz |
| BRAM_18K | 14 / 1200 |
| DSP | 10 / 1312 |
| FF | 6659 / 1041408 |
| LUT | 4989 / 520704 |
| URAM | 0 / 264 |

The generated pipeline therefore sustains one 55-bit modular MAC per clock
after filling, at the 312.5 MHz VEK280 platform clock. The four 64-bit AXI
ports imply a peak streaming demand of 32 bytes/cycle (three reads and one
write), so board-level performance will depend on memory-port placement and
whether operands remain resident across multiple CKKS operations.

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
