# The RV32I core block's power grid: IHP's pdn.tcl, plus the SRAM macro's supplies and its straps up to the core's stripes
####################################
# global connections
####################################
# standard cells
add_global_connection -net {VDD} -pin_pattern {^VDD$} -power
add_global_connection -net {VDD} -pin_pattern {^VDDPE$}
add_global_connection -net {VDD} -pin_pattern {^VDDCE$}
add_global_connection -net {VSS} -pin_pattern {^VSS$} -ground
add_global_connection -net {VSS} -pin_pattern {^VSSE$}
# the SRAM macro's supplies
add_global_connection -net {VDD} -pin_pattern {^VDD!$} -power
add_global_connection -net {VDD} -pin_pattern {^VDDARRAY!$} -power
add_global_connection -net {VSS} -pin_pattern {^VSS!$} -ground
global_connect
####################################
# voltage domains
####################################
set_voltage_domain -name {CORE} -power {VDD} -ground {VSS}
#####################################
# standard cell grid
####################################
define_pdn_grid -name {grid} -voltage_domains {CORE} -pins {TopMetal1 TopMetal2}
add_pdn_ring -grid {grid} -layers {TopMetal1 TopMetal2} -widths {5.0} -spacings {2.0} \
  -core_offsets {4.5} -connect_to_pads
add_pdn_stripe -grid {grid} -layer {Metal1} -width {0.44} -pitch {7.56} -offset {0} -followpins \
  -extend_to_core_ring
add_pdn_stripe -grid {grid} -layer {TopMetal1} -width {2.200} -pitch {75.6} -offset {13.600} \
  -extend_to_core_ring
add_pdn_stripe -grid {grid} -layer {TopMetal2} -width {2.200} -pitch {75.6} -offset {13.600} \
  -extend_to_core_ring
add_pdn_connect -grid {grid} -layers {Metal1 TopMetal1}
add_pdn_connect -grid {grid} -layers {TopMetal1 TopMetal2}
####################################
####################################
# the SRAM macro: its Metal4 supply straps up to the core's TopMetal1 stripes
####################################
define_pdn_grid -name {sram_grid} -voltage_domains {CORE} -macro -cells {RM_IHPSG13_1P_64x64_c2_bm_bist}
add_pdn_connect -grid {sram_grid} -layers {Metal4 TopMetal1}
