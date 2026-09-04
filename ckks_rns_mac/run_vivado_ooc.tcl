set script_dir [file dirname [file normalize [info script]]]
set rtl_dir [file join $script_dir build solution1 syn verilog]
set out_dir [file join $script_dir vivado_ooc]
set report_dir [file join $out_dir reports]
set part_name {xcve2802-vsvh1760-2MP-e-S}

# Vivado 2025.2 on this machine reports a corrupted per-user Tcl Store. Load
# the installation copy of the app initializer explicitly so ordinary project
# commands do not depend on the user catalog.
set install_tclstore {E:/AMDDesignTools/2025.2/Vivado/data/XilinxTclStore}
set ::env(XILINX_TCLAPP_REPO) [file join $install_tclstore tclapp]
lappend auto_path [file join $install_tclstore support appinit]
lappend auto_path [file join $install_tclstore tclapp]
package require ::tclapp::support::appinit 1.2

if {![file isdirectory $rtl_dir]} {
    error "HLS RTL directory not found: $rtl_dir (run run_hls.tcl first)"
}

file mkdir $out_dir
file mkdir $report_dir

create_project -force ckks_rns_mac_ooc $out_dir -part $part_name
set_property target_language Verilog [current_project]

set rtl_files [glob -nocomplain [file join $rtl_dir *.v]]
if {[llength $rtl_files] == 0} {
    error "No HLS Verilog files found in $rtl_dir"
}
read_verilog $rtl_files

set top_name ckks_rns_mac
synth_design -top $top_name -part $part_name -mode out_of_context
create_clock -name ap_clk -period 3.2 [get_ports ap_clk]

write_checkpoint -force [file join $out_dir post_synth.dcp]
report_utilization -hierarchical -file \
    [file join $report_dir post_synth_utilization.rpt]
report_timing_summary -delay_type max -max_paths 20 -file \
    [file join $report_dir post_synth_timing.rpt]

opt_design
place_design
phys_opt_design
route_design

write_checkpoint -force [file join $out_dir post_route.dcp]
report_utilization -hierarchical -file \
    [file join $report_dir post_route_utilization.rpt]
report_timing_summary -delay_type max -max_paths 20 -report_unconstrained \
    -file [file join $report_dir post_route_timing.rpt]
report_route_status -file [file join $report_dir route_status.rpt]

puts "VIVADO_OOC_DONE"
puts "Reports: $report_dir"
exit
