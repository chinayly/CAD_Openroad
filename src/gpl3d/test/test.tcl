############################################################
# 3D Timing-driven Flow (OpenROAD + Your gpl3d)
# Uses: TimingOracle::registerTcl -> gpl3d::td::timing_iteration
# Criticality->weight feedback is done in Tcl and applied via your hooks.
############################################################

# ===================== 基本路径 =====================
set ALL_LEFS {
  /home/TritonPart/TD_3D/test/Nangate45/Nangate45_tech.lef
    /home/testcase/iccad2015.ot/superblue1/superblue1.lef
    
}
set ALL_DEFS {/home/testcase/iccad2015.ot/simple/simple_QQ.def}
set LATE_LIBS {
  /home/testcase/iccad2015.ot/superblue1/superblue1_Late.lib
}

set design "simple"
set top_design "simple"
set netlist "/home/testcase/iccad2015.ot/simple/simple.v"
set sdc     "/home/testcase/iccad2015.ot/simple/simple.sdc"

# ===================== 单位（与库一致；在 read_liberty 前） =====================
set_units -time ns -capacitance ff -resistance kohm -voltage V -current mA -distance um

# ===================== 读库顺序 =====================
foreach lib_file ${LATE_LIBS} { read_liberty $lib_file }
read_verilog $netlist
foreach lef_file ${ALL_LEFS} { read_lef $lef_file }
read_def [lindex ${ALL_DEFS} 0]
link_design $top_design
read_sdc $sdc

# ===================== RC/STA 基线设置 =====================
set_wire_rc -layer metal3
catch { set_propagated_clock [all_clocks] }
catch { set_thread_count 8 }

# ===================== 工具函数：命令探测/安全调用 =====================
proc __have {cmd} { expr {[llength [info commands $cmd]] > 0} }
proc __call {cmd args} {
  if {[__have $cmd]} { uplevel [list $cmd] $args; return 1 }
  return 0
}

# 你工程可能提供的 Tcl 入口
set IMPORT_PLACEDB_CMDS   { gpl3d::import_place_db }
# 权重回灌命令（任一存在即可）
set APPLY_WEIGHT_CMDS     { gpl3d::td::apply_net_weights gpl3d::apply_sta_weights gpl3d::net_weight_scheduler_apply }

# ===================== 参数（可按需调整） =====================
set CRIT_EPS   1e-9
set ALPHA      1.5
set GAMMA      2.0
set WMIN       0.5
set WMAX       3.0
set EMA        0.6
set MAX_ROWS   0     ;# report_checks 路径数量上限；0=不限

# ===================== 从 report_checks 聚合 per-net 最差 slack(ns) =====================
proc __collect_net_slack_maxmode {{max_rows 0}} {
  set result [dict create]
  set args {-path_delay max -fields {net slack} -digits 6}
  if {$max_rows > 0} { lappend args -max_paths $max_rows }
  set lines [eval report_checks $args]
  foreach line [split $lines "\n"] {
    if {[string match "*slack*" $line] || [string trim $line] eq ""} { continue }
    set fields [regexp -inline -all {\S+} $line]
    if {[llength $fields] < 2} { continue }
    set net [lindex $fields 0]
    set s   [lindex $fields end]
    if {$net eq "-" || ![string is double -strict $s]} { continue }
    set s [expr {double($s)}]
    if {![dict exists $result $net] || $s < [dict get $result $net]} {
      dict set result $net $s
    }
  }
  return $result
}

# ===================== slack -> criticality[0,1]（基于 WNS 归一化） =====================
proc __slack_to_crit {net2slack wns_setup} {
  set crit [dict create]
  set denom [expr {abs(double($wns_setup)) + $::CRIT_EPS}]
  dict for {n s} $net2slack {
    set c 0.0
    if {$s < 0.0} {
      set c [expr {(-$s)/$denom}]
      if {$c > 1.0} { set c 1.0 }
    }
    dict set crit $n $c
  }
  return $crit
}

# ===================== criticality -> weight（可选 EMA） =====================
proc __crit_to_weights {crit {prev_weights {}}} {
  set weights [dict create]
  dict for {n c} $crit {
    set w [expr {1.0 + $::ALPHA * pow($c, $::GAMMA)}]
    if {$w < $::WMIN} { set w $::WMIN }
    if {$w > $::WMAX} { set w $::WMAX }
    if {$::EMA > 0.0 && [dict exists $prev_weights $n]} {
      set w_prev [dict get $prev_weights $n]
      set w [expr {$::EMA*$w_prev + (1.0-$::EMA)*$w}]
    }
    dict set weights $n $w
  }
  return $weights
}

# ===================== 应用权重到放置器（尝试多候选命令名） =====================
proc __apply_weights {weights} {
  foreach cmd $::APPLY_WEIGHT_CMDS {
    if {[__have $cmd]} {
      set rc [catch {$cmd $weights} err]
      if {!$rc} { puts "INFO: applied net weights via $cmd"; return 1 }
      puts "WARN: $cmd failed: $err"
    }
  }
  puts "WARN: no weight-apply hook found. Please expose one of: $::APPLY_WEIGHT_CMDS"
  return 0
}

# ===================== 单轮：估寄生 → C++更新时序 → 提取criticality → 回灌权重 =====================
proc timing_iter_and_feedback {{tag ""}} {
  puts "== [clock format [clock seconds] -format {%H:%M:%S}] TIMING+FEEDBACK $tag =="

  # 1) placement RC 估计（2D 线长）
  catch { estimate_parasitics -placement }

  # 2) C++：调用 TimingOracle 的 Tcl 钩子（需在 C++ 初始化时 registerTcl）
  if {![__have gpl3d::td::timing_iteration]} {
    puts "FATAL: gpl3d::td::timing_iteration not found. Did you call TimingOracle::registerTcl(interp)?"
    return
  }
  set sum [gpl3d::td::timing_iteration -hold 0]
  set WNS_setup [dict get $sum WNS_setup]
  set TNS_setup [dict get $sum TNS_setup]
  puts "STA: WNS=[format %.3f $WNS_setup] ns, TNS=[format %.3f $TNS_setup] ns"

  # 3) 从 OpenSTA 拉 per-net 最差 slack → 转 criticality
  set net2slack [__collect_net_slack_maxmode $::MAX_ROWS]
  set net2crit  [__slack_to_crit $net2slack $WNS_setup]

  # 4) 映射为权重并做 EMA 平滑
  if {![info exists ::gpl3d::td::prev_weights]} { set ::gpl3d::td::prev_weights {} }
  set weights [__crit_to_weights $net2crit $::gpl3d::td::prev_weights]
  set ::gpl3d::td::prev_weights $weights

  # 5) 应用权重
  __apply_weights $weights

  # 6) spef 便于排查（可选）
  catch { write_parasitics -spef timing_iter_${tag}.spef }
}

# ===================== 进入 ePlace3D =====================
puts "Start placer3d"

# Debug 开关
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

# 导入 OpenDB -> PlaceDB（探测式调用）
set imported 0
foreach c $::IMPORT_PLACEDB_CMDS {
  if {[__call $c]} { set imported 1; break }
}
if {!$imported} {
  puts "WARN: place DB import hook not found (tried: $::IMPORT_PLACEDB_CMDS)."
}

# 主放置（会在内部创建 TimingManager，之后 TCL 命令可用）
gpl3d::placer3d_run

# 放置后：进行时序迭代和反馈
# 注意：TimingManager 在 placer3d_run 内部创建，所以现在可以调用 timing_iteration
timing_iter_and_feedback POST1
timing_iter_and_feedback POST2

# 注释掉：这些网络名称是针对 public_case1 的，在 public_case2 中可能不存在
# report_net  rof1_0__core/fe/bp_fe_pc_gen_1/_0582_
# report_net  rof1_0__core/fe_cmd_fifo.mem_1r1w.r_v_i

