$display("Running DRIVER TEST...");

base = 32'h43C1_1000;
addr = 32'h1000_0000;
read_addr = 32'h1000_0000;
irq_count = 0;
total_samples = 0;
pcap_completed = 0;
arm = 0;
enable = 0;
capture = 0;

PGEN_REPEAT  = 2;
PGEN_SAMPLES = 5;

$display("Start Preload \n");
tb.uut.ps.ps.ps.inst.pre_load_mem_from_file("preload_ddr.txt",32'h2000_0000,65536);

tb.uut.ps.ps.ps.inst.fpga_soft_reset(32'h1);
tb.uut.ps.ps.ps.inst.fpga_soft_reset(32'h0);

repeat(125) @(posedge tb.uut.ps.FCLK);

REG_WRITE(PGEN_BASE, PGEN_CYCLES, PGEN_REPEAT);
REG_WRITE(PGEN_BASE, PGEN_TABLE_ADDRESS, 32'h2000_0000);
REG_WRITE(PGEN_BASE, PGEN_TABLE_LENGTH, 4* PGEN_SAMPLES);   // in bytes
REG_WRITE(PGEN_BASE, PGEN_ENABLE, 106);                     // PCAP_ACTIVE
REG_WRITE(PGEN_BASE, PGEN_TRIG, 38);                        // SEQ_OUTA

REG_WRITE(PGEN_BASE+32'h100, PGEN_CYCLES, PGEN_REPEAT);
REG_WRITE(PGEN_BASE+32'h100, PGEN_TABLE_ADDRESS, 32'h2000_8000);
REG_WRITE(PGEN_BASE+32'h100, PGEN_TABLE_LENGTH, 4* PGEN_SAMPLES);   // in bytes
REG_WRITE(PGEN_BASE+32'h100, PGEN_ENABLE, 106);             // PCAP_ACTIVE
REG_WRITE(PGEN_BASE+32'h100, PGEN_TRIG, 38);                // SEQ_OUTA

repeat(12500) @(posedge tb.uut.ps.FCLK);

// Setup a sequencer to output 10 pulses with 200usec period.
REG_WRITE(SEQ_BASE, SEQ_PRESCALE, 1);          // 1usec
REG_WRITE(SEQ_BASE, SEQ_TABLE_CYCLE, 1);        // Don't repeat

REG_WRITE(SEQ_BASE, SEQ_TABLE_START, 0);
REG_WRITE(SEQ_BASE, SEQ_TABLE_DATA, PGEN_REPEAT * PGEN_SAMPLES);  // Repeats
REG_WRITE(SEQ_BASE, SEQ_TABLE_DATA, 32'h1F003F00);
REG_WRITE(SEQ_BASE, SEQ_TABLE_DATA, 1);         // 1us on
REG_WRITE(SEQ_BASE, SEQ_TABLE_DATA, 1);         // 1us off
REG_WRITE(SEQ_BASE, SEQ_TABLE_LENGTH, 1 * 4);   // # of DWORDs

REG_WRITE(SEQ_BASE, SEQ_ENABLE, 106);           // PCAP_ACTIVE
REG_WRITE(SEQ_BASE, SEQ_INPA, 1);               // BITS_ONE

// Setup Position Capture
REG_WRITE(REG_BASE, REG_PCAP_START_WRITE, 1);
REG_WRITE(REG_BASE, REG_PCAP_WRITE, 20);        // PGEN-0

REG_WRITE(PCAP_BASE, PCAP_ENABLE,  62);         // SEQ_ACTIVE
REG_WRITE(PCAP_BASE, PCAP_FRAME,   0);          // BITS_ZER0
REG_WRITE(PCAP_BASE, PCAP_CAPTURE, 38);         // SEQ_OUTA

REG_WRITE(REG_BASE, REG_PCAP_FRAMING_MASK, 0);
REG_WRITE(REG_BASE, REG_PCAP_FRAMING_ENABLE, 0);
REG_WRITE(REG_BASE, REG_PCAP_FRAMING_MODE, 0);

REG_WRITE(DRV_BASE, DRV_PCAP_BLOCK_SIZE, tb.BLOCK_SIZE);
REG_WRITE(DRV_BASE, DRV_PCAP_TIMEOUT, 0);

repeat(125) @(posedge tb.uut.ps.FCLK);

ARMS = 1;

fork

// Generate consecutive ARM signals.
begin
    for (k = 0; k < ARMS; k = k + 1) begin
        REG_WRITE(DRV_BASE, DRV_PCAP_DMA_RESET, 1);     // DMA Reset
        REG_WRITE(DRV_BASE, DRV_PCAP_DMA_ADDR, addr);   // DMA Addr
        REG_WRITE(DRV_BASE, DRV_PCAP_DMA_START, 1);     // DMA Start
        addr = addr + tb.BLOCK_SIZE;                    //
        REG_WRITE(DRV_BASE, DRV_PCAP_DMA_ADDR, addr);   // DMA Addr
        repeat(125) @(posedge tb.uut.ps.FCLK);
        REG_WRITE(REG_BASE, REG_PCAP_ARM, 1);           // PCAP Arm
        wait (pcap_completed == 1);
        //
        // Clear and Wait for new ARM
        //
        pcap_completed = 0;
        irq_count = 0;
        total_samples = 0;
        addr = 32'h1000_0000;
        read_addr = 32'h1000_0000;
        // Gap until next arming.
        repeat(12500) @(posedge tb.uut.FCLK_CLK0);
    end
    $finish;
end

begin
end
    `include "./irq_handler.v"
join

repeat(1250) @(posedge tb.uut.ps.FCLK);
$finish;