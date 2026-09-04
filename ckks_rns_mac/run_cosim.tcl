set script_dir [file dirname [file normalize [info script]]]
set build_dir [file join $script_dir build]

open_project $build_dir
open_solution solution1

puts "=== CKKS RNS MAC: C/RTL CO-SIMULATION ==="
cosim_design -rtl verilog -trace_level none

exit
