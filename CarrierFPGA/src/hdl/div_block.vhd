--------------------------------------------------------------------------------
--  PandA Motion Project - 2016
--      Diamond Light Source, Oxford, UK
--      SOLEIL Synchrotron, GIF-sur-YVETTE, France
--
--  Author      : Dr. Isa Uzun (isa.uzun@diamond.ac.uk)
--------------------------------------------------------------------------------
--
--  Description : Programmable Divider Block top-level module.
--
--------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.top_defines.all;

entity div_block is
port (
    -- Clock and Reset
    clk_i               : in  std_logic;
    reset_i             : in  std_logic;
    -- Memory Bus Interface
    mem_cs_i            : in  std_logic;
    mem_wstb_i          : in  std_logic;
    mem_addr_i          : in  std_logic_vector(BLK_AW-1 downto 0);
    mem_dat_i           : in  std_logic_vector(31 downto 0);
    mem_dat_o           : out std_logic_vector(31 downto 0);
    -- Block inputs
    sysbus_i            : in  sysbus_t;
    -- Output pulse
    outd_o              : out std_logic;
    outn_o              : out std_logic
);
end div_block;

architecture rtl of div_block is

signal INP_VAL          : std_logic_vector(31 downto 0);
signal ENABLE_VAL       : std_logic_vector(31 downto 0);
signal FIRST_PULSE      : std_logic_vector(31 downto 0);
signal DIVISOR          : std_logic_vector(31 downto 0);
signal COUNT            : std_logic_vector(31 downto 0);
signal DIVISOR_WSTB     : std_logic;
signal FIRST_PULSE_WSTB : std_logic;

signal inp              : std_logic;
signal enable           : std_logic;

begin

--
-- Control System Interface
--
div_ctrl : entity work.div_ctrl
port map (
    clk_i               => clk_i,
    reset_i             => reset_i,
    sysbus_i            => sysbus_i,
    posbus_i            => (others => (others => '0')),
    inp_o               => inp,
    enable_o            => enable,

    mem_cs_i            => mem_cs_i,
    mem_wstb_i          => mem_wstb_i,
    mem_addr_i          => mem_addr_i,
    mem_dat_i           => mem_dat_i,
    mem_dat_o           => mem_dat_o,

    DIVISOR             => DIVISOR,
    DIVISOR_WSTB        => DIVISOR_WSTB,
    FIRST_PULSE         => FIRST_PULSE,
    FIRST_PULSE_WSTB    => FIRST_PULSE_WSTB,
    COUNT               => COUNT
);

-- LUT Block Core Instantiation
div : entity work.div
port map (
    clk_i               => clk_i,
    reset_i             => reset_i,

    inp_i               => inp,
    enable_i            => enable,
    outd_o              => outd_o,
    outn_o              => outn_o,

    DIVISOR             => DIVISOR,
    DIVISOR_WSTB        => DIVISOR_WSTB,
    FIRST_PULSE         => FIRST_PULSE(0),
    FIRST_PULSE_WSTB    => FIRST_PULSE_WSTB,

    COUNT               => COUNT
);

end rtl;
