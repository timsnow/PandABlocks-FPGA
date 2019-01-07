# -------------------------------------------------------------------
# Define asynchronous clocks
# -------------------------------------------------------------------

# --------------------------------------------------------------------
# EVENT RECEIVER
# --------------------------------------------------------------------

create_generated_clock -name clk1mux -divide_by 1 -add -master_clock clk_fpga_0 -source [get_pins SFP_GEN.sfp_inst/sfp_mmcm_clkmux_inst/BUFGMUX_inst/I0] [get_pins SFP_GEN.sfp_inst/sfp_mmcm_clkmux_inst/BUFGMUX_inst/O]
create_generated_clock -name clk2mux -divide_by 1 -add -master_clock EXTCLK_P -source [get_pins SFP_GEN.sfp_inst/sfp_mmcm_clkmux_inst/BUFGMUX_inst/I1] [get_pins SFP_GEN.sfp_inst/sfp_mmcm_clkmux_inst/BUFGMUX_inst/O]
set_clock_groups -physically_exclusive -group clk1mux -group clk2mux

create_generated_clock -name clk1mux_cas -divide_by 1 -add -master_clock clk1mux -source [get_pins SFP_GEN.sfp_inst/sfp_mmcm_clkmux_inst/eventr_BUFGMUX_inst/I0] [get_pins SFP_GEN.sfp_inst/sfp_mmcm_clkmux_inst/eventr_BUFGMUX_inst/O]
create_generated_clock -name clk2mux_cas -divide_by 1 -add -master_clock clk2mux -source [get_pins SFP_GEN.sfp_inst/sfp_mmcm_clkmux_inst/eventr_BUFGMUX_inst/I0] [get_pins SFP_GEN.sfp_inst/sfp_mmcm_clkmux_inst/eventr_BUFGMUX_inst/O]
create_generated_clock -name clk3mux -divide_by 1 -add -master_clock SFP_GEN.sfp_inst/sfpgtx_event_receiver_inst/event_receiver_mgt_inst/U0/event_receiver_mgt_i/gt0_event_receiver_mgt_i/gtxe2_i/RXOUTCLK -source [get_pins SFP_GEN.sfp_inst/sfp_mmcm_clkmux_inst/eventr_BUFGMUX_inst/I1] [get_pins SFP_GEN.sfp_inst/sfp_mmcm_clkmux_inst/eventr_BUFGMUX_inst/O]
set_clock_groups -physically_exclusive -group clk1mux_cas -group clk2mux_cas -group clk3mux


set_clock_groups -asynchronous  \
-group clk_fpga_0 \
-group FMC_CLK0_M2C_P \
-group FMC_CLK1_M2C_P \
-group [get_clocks -filter {NAME =~ *TXOUTCLK}] \
-group EXTCLK_P \
-group [get_clocks GTXCLK0_P] \


set_clock_groups -asynchronous \
-group [get_clocks clk2mux_cas] \
-group [get_clocks clk_fpga_0] \
-group [get_clocks GTXCLK0_P] \


set_clock_groups -asynchronous \
-group [get_clocks GTXCLK0_P] \
-group [get_clocks clk1mux_cas] \
-group [get_clocks clk2mux_cas] \
-group [get_clocks SFP_GEN.sfp_inst/sfpgtx_event_receiver_inst/event_receiver_mgt_inst/U0/event_receiver_mgt_i/gt0_event_receiver_mgt_i/gtxe2_i/RXOUTCLK]


set_clock_groups -asynchronous \
-group [get_clocks clk_fpga_0] \
-group [get_clocks clk1mux_cas] \
-group [get_clocks clk2mux_cas] \
-group [get_clocks clk3mux] \
-group [get_clocks GTXCLK0_P] \


set_clock_groups -asynchronous \
-group [get_clocks clk3mux] \
-group [get_clocks clk1mux] \
-group [get_clocks clk2mux] \


set_clock_groups -asynchronous \
-group [get_clocks clk2mux_cas] \
-group [get_clocks clk1mux] \


set_clock_groups -asynchronous \
-group [get_clocks clk1mux_cas] \
-group [get_clocks clk2mux] \


# -------------------------------------------------------------------
# BUFGCTRL placement problem, this uses is highly discouraged but
# there is no way around it.
# -------------------------------------------------------------------
# BUFGMUX
set_property CLOCK_DEDICATED_ROUTE FALSE [get_nets ps/processing_system7_0/inst/FCLK_CLK0]


# BUFGMUX enable_sma_clock register to CE0 (chip Enable) on the mux
set_false_path -from [get_pins SFP_GEN.sfp_inst/sfp_mmcm_clkmux_inst/enable_sma_clock_reg/C] -to [get_pins SFP_GEN.sfp_inst/sfp_mmcm_clkmux_inst/BUFGMUX_inst/CE0]
set_false_path -from [get_pins SFP_GEN.sfp_inst/sfp_mmcm_clkmux_inst/enable_sma_clock_reg/C] -to [get_pins SFP_GEN.sfp_inst/sfp_mmcm_clkmux_inst/BUFGMUX_inst/CE1]


# Pack IOB registers
set_property iob true [get_cells -hierarchical -regexp -filter {NAME =~ .*iob_reg.*}]

