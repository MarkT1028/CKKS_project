set parts [get_parts -quiet *ve2802*]
puts "VE2802_PARTS_BEGIN"
foreach p $parts { puts $p }
puts "VE2802_PARTS_END"

set boards [get_board_parts -quiet *vek280*]
puts "VEK280_BOARDS_BEGIN"
foreach b $boards { puts $b }
puts "VEK280_BOARDS_END"

exit
