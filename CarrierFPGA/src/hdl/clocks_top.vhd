library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.top_defines.all;

entity clocks_top is
port (
    -- Clock and Reset
    clk_i               : in  std_logic;
    reset_i             : in  std_logic;
    -- Memory Bus Interface
    mem_addr_i          : in  std_logic_vector(PAGE_AW-1 downto 0);
    mem_cs_i            : in  std_logic;
    mem_wstb_i          : in  std_logic;
    mem_rstb_i          : in  std_logic;
    mem_dat_i           : in  std_logic_vector(31 downto 0);
    mem_dat_o           : out std_logic_vector(31 downto 0);
    -- Output pulses
    clocks_a_o          : out std_logic;
    clocks_b_o          : out std_logic;
    clocks_c_o          : out std_logic;
    clocks_d_o          : out std_logic
);
end clocks_top;

architecture rtl of clocks_top is

begin

-- Unused outputs.
mem_dat_o <= (others => '0');

--
-- Instantiate BITS Blocks :
--  There are BITS_NUM amount of encoders on the board
--
clocks_block : entity work.clocks_block
port map (
    clk_i               => clk_i,
    reset_i             => reset_i,

    mem_cs_i            => mem_cs_i,
    mem_wstb_i          => mem_wstb_i,
    mem_addr_i          => mem_addr_i(BLK_AW-1 downto 0),
    mem_dat_i           => mem_dat_i,

    clocks_a_o          => clocks_a_o,
    clocks_b_o          => clocks_b_o,
    clocks_c_o          => clocks_c_o,
    clocks_d_o          => clocks_d_o
);

end rtl;