# set technology information
set ALL_LEFS {
    /home/testcase/pdk/lef/NangateOpenCellLibrary.tech.lef
    /home/testcase/pdk/lef/NangateOpenCellLibrary.macro.mod.lef
    /home/testcase/pdk/lef/fakeram45_32x64.lef
    /home/testcase/pdk/lef/fakeram45_64x7.lef
    /home/testcase/pdk/lef/fakeram45_64x96.lef
    /home/testcase/pdk/lef/fakeram45_256x96.lef
    /home/testcase/pdk/lef/fakeram45_512x64.lef

}
set ALL_DEFS {
    /home/testcase/Public/public_case1/input.def
}
set EARLY_LIBS {
    /home/testcase/pdk/lib/NangateOpenCellLibrary_typical.lib
    
}
set LATE_LIBS {
    /home/testcase/pdk/lib/NangateOpenCellLibrary_typical.lib
    /home/testcase/pdk/lib/fakeram45_64x96.lib
    /home/testcase/pdk/lib/fakeram45_256x96.lib
    /home/testcase/pdk/lib/fakeram45_512x64.lib
    /home/testcase/pdk/lib/fakeram45_64x7.lib
}
# /home/testcase/iccad2015.ot/superblue1/superblue1_Early.lib
# set design information
set design "top"
set top_design "top"
set netlist "/home/testcase/Public/public_case1/input.v"
set sdc "/home/testcase/Public/public_case1/input.sdc" 

# proc set_all_input_output_delays {{clk_period_factor .2}} {
#   set clk [lindex [all_clocks] 0]
#   set period [get_property $clk period]
#   set delay [expr $period * $clk_period_factor]
#   set_input_delay $delay -clock $clk [delete_from_list [all_inputs] [all_clocks]]
#   set_output_delay $delay -clock $clk [delete_from_list [all_outputs] [all_clocks]]
# }

read_verilog $netlist

# foreach lib_file ${EARLY_LIBS} {
#     read_lib $lib_file
# }
foreach lib_file ${LATE_LIBS} {
    read_liberty $lib_file
}
foreach lef_file ${ALL_LEFS} {
  read_lef $lef_file
}
foreach def_file ${ALL_DEFS} {
  read_def $def_file -continue_on_errors
}

link_design $top_design
read_sdc $sdc

##############################################################################################
## Try placer3d
##############################################################################################
puts "Start placer3d"

# set the debug level
set_debug_level GPL3D "run" 1
set_debug_level PAR "initial_partitioning" 1
set_debug_level PAR "multilevel_partitioning" 1
set_debug_level PAR "v_cycle_refinement" 1
set_debug_level PAR "coarsening" 1
set_debug_level PAR "netlist" 1
set_debug_level PAR "refinement" 1
set_debug_level PAR "cut_overlay_clustering" 1
set_debug_level PAR "evaluation" 1
set_debug_level PAR "partitioning" 1

# call triton_part to partition the netlist

#placer3d_run -solution_file "/home/TritonPart/TD_3D/src/gpl3d/test/${design}_placer3d_solution.txt"
gpl3d::import_place_db 
gpl3d::placer3d_run