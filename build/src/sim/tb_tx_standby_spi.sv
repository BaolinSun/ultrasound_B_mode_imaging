`timescale 1ns/1ps
// Real four-engine serialization: a busy die cannot be hidden by CFG_IDLE.
module tb_tx_standby_spi;
    logic clk_ctrl=0,clk_tx=0,rst_n=0;
    always #5 clk_ctrl=~clk_ctrl;
    always #2.5 clk_tx=~clk_tx;
    logic [23:0] addr=0;
    logic [31:0] wdata=0,rdata;
    logic en=0;
    logic [3:0] we=0,trig=0,spi_idle,sen,done;
    logic scan_busy=0,ctrl_ready=1,tx_hold,tx_standby;
    wire tx_idle=(&spi_idle)&&!scan_busy;
    tx_power_hw dut(.*);
    for(genvar n=0;n<4;n++)begin:die
        tx_spi engine(.clk(clk_tx),.reset(!rst_n),.spi_trig(trig[n]),
            .spi_mode(n%2==1),.spi_addr(10'h018),.spi_txdata(32'ha5a5f00f),
            .spi_rxdata(),.spi_rxvalid(),.spi_done(done[n]),.spi_rdnext(),
            .o_spi_sen(sen[n]),.o_spi_sclk(),.o_spi_sdata(),.i_spi_sdout(1'b0),.power_idle(spi_idle[n]));
    end
    task wr(input [5:0] offset,input [31:0] value);
        @(negedge clk_ctrl);addr=24'h4c0340+offset;wdata=value;en=1;we=15;
        @(negedge clk_ctrl);en=0;we=0;
    endtask
    task wait_ack(input [31:0] value);
        addr=24'h4c034c;
        repeat(10000)begin @(negedge clk_ctrl);if(rdata==value)return;end
        $fatal(1,"real SPI drain timeout");
    endtask
    initial begin
        repeat(8)@(negedge clk_ctrl);rst_n=1;
        repeat(8)@(negedge clk_ctrl);
        scan_busy=1;
        // Stagger requests: both writing and reading are active at commit.
        for(int n=0;n<4;n++)begin
            @(negedge clk_tx);trig[n]=1;@(negedge clk_tx);trig[n]=0;repeat(5)@(negedge clk_tx);
        end
        wr(4,3);wr(8,1);repeat(30)@(negedge clk_ctrl);
        if(tx_standby||tx_hold)$fatal(1,"standby interrupted a scan");
        scan_busy=0;wait_ack(1);
        if(!tx_standby||!tx_hold||sen!=15||spi_idle!=15)$fatal(1,"in-flight die lost");
        wr(4,1);wr(8,2);wait_ack(2);if(tx_standby||!tx_hold)$fatal;
        wr(4,0);wr(8,3);wait_ack(3);if(tx_standby||tx_hold)$fatal;
        $display("PASS tx_standby_spi four real shifters/read+write/drain/no cancellation");$finish;
    end
    always @(posedge clk_tx)if(rst_n && tx_standby && (!(&sen)||!(&spi_idle)||scan_busy))$fatal(1,"unsafe STDBY");
    initial begin #200000;$fatal(1,"watchdog");end
endmodule
