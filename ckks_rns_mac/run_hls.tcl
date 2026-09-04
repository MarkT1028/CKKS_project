set script_dir [file dirname [file normalize [info script]]]
set build_dir [file join $script_dir build]

open_project -reset $build_dir
set_top ckks_rns_mac
add_files [file join $script_dir src ckks_rns_mac.cpp] \
    -cflags "-I[file join $script_dir src]"
add_files -tb [file join $script_dir tb test_ckks_rns_mac.cpp] \
    -cflags "-I[file join $script_dir src]"

open_solution -reset solution1 -flow_target vivado
set_part {xcve2802-vsvh1760-2MP-e-S}
create_clock -period 3.2 -name default

puts "=== CKKS RNS MAC: C SIMULATION ==="
csim_design

puts "=== CKKS RNS MAC: C SYNTHESIS ==="
csynth_design

exit
