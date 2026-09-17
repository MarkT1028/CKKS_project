set script_dir [file dirname [file normalize [info script]]]
set build_dir [file join $script_dir build_openfhe_vector]

if {![info exists ::env(CKKS_CT_MULT_VECTOR_FILE)] ||
    $::env(CKKS_CT_MULT_VECTOR_FILE) eq ""} {
    error "CKKS_CT_MULT_VECTOR_FILE is not set"
}
set vector_file [file normalize $::env(CKKS_CT_MULT_VECTOR_FILE)]
if {![file exists $vector_file]} {
    error "OpenFHE ciphertext vector not found: $vector_file"
}

puts "=== OpenFHE complete ciphertext vector: $vector_file ==="

open_project -reset $build_dir
set_top ckks_ciphertext_mult

add_files [file join $script_dir src ckks_ciphertext_mult.cpp] \
    -cflags "-I[file join $script_dir src]"
add_files -tb \
    [file join $script_dir tb test_ckks_ciphertext_mult_openfhe.cpp] \
    -cflags "-I[file join $script_dir src]"

open_solution -reset solution1 -flow_target vivado
set_part {xcve2802-vsvh1760-2MP-e-S}
create_clock -period 3.2 -name default

puts "=== OPENFHE C SIMULATION ==="
csim_design

puts "=== C SYNTHESIS ==="
csynth_design

puts "=== OPENFHE C/RTL CO-SIMULATION ==="
cosim_design -rtl verilog -trace_level none

exit
