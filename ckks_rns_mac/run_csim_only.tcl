set script_dir [file dirname [file normalize [info script]]]
set build_dir [file join $script_dir build]

open_project $build_dir
open_solution solution1

puts "=== CKKS RNS MAC: C SIMULATION ONLY ==="
csim_design -clean

exit
