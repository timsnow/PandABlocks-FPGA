# -------------------------------------------------------------------
# SFP MGTs - Bank 112
# -------------------------------------------------------------------

# -------------------------------------------------------------------
# Define asynchronous clocks
# -------------------------------------------------------------------
set_clock_groups -asynchronous -group [get_clocks -filter {NAME =~ *TXOUTCLK}]
