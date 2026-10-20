`timescale 1ns/1ps
module tb_ddr_frame_store;
    reg clk_axi=0,clk_in=0,rst_n=0;
    always #5 clk_axi=~clk_axi;
    always #20 clk_in=~clk_in;
    reg [15:0] in_data=0;
    reg in_valid=0,in_header=0,in_last=0,in_busy=0;
    reg [23:0] ctrl_addr=0;
    reg [31:0] ctrl_wdata=0;
    reg ctrl_en=0;reg [3:0] ctrl_we=0;
    wire [31:0] ctrl_rdata,awaddr;wire [7:0] awlen;
    wire awvalid,wvalid,wlast,bready,frame_done;
    wire [63:0] wdata;
    reg awready=0,wready=0,bvalid=0;
    reg [1:0] bresp=0;
    reg inject_error=0,block_axi=0;
    ddr_frame_store #(.FIFO_DEPTH(512),.FRAME_TIMEOUT_CYCLES(3000000)) dut(.*);
    reg [63:0] memory[0:393215];
    integer remaining=0,address=0,publications=0,bursts=0,short_bursts=0;
    reg previous_w_stall=0,previous_aw_stall=0;
    reg [63:0] previous_wdata;reg previous_wlast;
    reg [31:0] previous_awaddr;reg [7:0] previous_awlen;
    reg [31:0] rng=32'h12345678;
    always @(posedge clk_axi) begin
        rng<={rng[30:0],rng[31]^rng[21]^rng[1]^rng[0]};
        awready<=!block_axi && (rng[1:0]!=0);
        wready<=!block_axi && (rng[3:2]!=0);
        if(!rst_n) begin bvalid<=0;remaining<=0;end
        else begin
            if(previous_w_stall && (wdata!==previous_wdata || wlast!==previous_wlast || !wvalid))
                $fatal(1,"W data changed under backpressure");
            if(previous_aw_stall && (awaddr!==previous_awaddr || awlen!==previous_awlen || !awvalid))
                $fatal(1,"AW changed under backpressure");
            if(awvalid && awready) begin
                if(remaining!=0 || bvalid) $fatal(1,"more than one outstanding burst");
                if(awaddr<32'h20000000 || awaddr>=32'h20300000 ||
                   (awaddr[11:0]+(awlen+1)*8)>4096) $fatal(1,"address/4KiB violation");
                if(dut.slot_state[(awaddr-32'h20000000)>>20]==3)
                    $fatal(1,"overwriting a leased frame");
                address<=(awaddr-32'h20000000)>>3;remaining<=awlen+1;
                bursts<=bursts+1;if(awlen==127) short_bursts<=short_bursts+1;
            end
            if(wvalid && wready) begin
                if(remaining==0 || wlast!=(remaining==1)) $fatal(1,"W length/last");
                memory[address]<=wdata;address<=address+1;remaining<=remaining-1;
                if(remaining==1) begin bvalid<=1;bresp<=inject_error?2:0;end
            end
            if(bvalid && bready) bvalid<=0;
            if(frame_done) begin
                if(remaining!=0 || bvalid) $fatal(1,"frame published before B");
                publications<=publications+1;
            end
        end
        previous_w_stall<=rst_n && wvalid && !wready;
        previous_wdata<=wdata;previous_wlast<=wlast;
        previous_aw_stall<=rst_n && awvalid && !awready;
        previous_awaddr<=awaddr;previous_awlen<=awlen;
    end
    task write_reg(input integer offset,input reg [31:0] value);
        @(negedge clk_axi);ctrl_addr=24'h4c0200+offset;ctrl_wdata=value;ctrl_en=1;ctrl_we=15;
        @(negedge clk_axi);ctrl_en=0;ctrl_we=0;
    endtask
    integer command_seq=0;
    task command(input integer opcode);
        command_seq=command_seq+1;
        write_reg(4,opcode);write_reg(8,command_seq);
        for(integer tries=0;tries<4000000;tries=tries+1) begin
            @(negedge clk_axi);
            if(dut.ack==command_seq) return;
        end
        $fatal(1,"command timeout op=%0d maintenance=%0d active=%b",opcode,dut.maintenance,dut.active);
    endtask
    task word(input reg [15:0] value,input bit head);
        @(negedge clk_in);in_data=value;in_valid=1;in_header=head;in_last=0;
    endtask
    task send_line(input integer line_no,input integer decimation);
        word(16'h1c00,1);word(6144,1);word(line_no,1);word(decimation,1);
        word(0,1);word(0,1);word(16'h1234,1);word(16'h5678,1);
        for(integer n=0;n<(6144>>decimation);n=n+1) begin
            word((line_no*17+n)^16'h1234,0);
            if(decimation==0 && n==6143) in_last=1;
        end
        @(negedge clk_in);in_valid=0;in_header=0;in_last=(decimation!=0);
        @(negedge clk_in);in_last=0;
        repeat(30) @(negedge clk_in);
    endtask
    task send_frame(input integer decimation);
        in_busy=1;
        for(integer line_no=0;line_no<64;line_no=line_no+1) send_line(line_no,decimation);
        in_busy=0;
    endtask
    task await_publication(input integer count);
        for(integer tries=0;tries<100000;tries=tries+1) begin
            @(negedge clk_axi);if(publications==count) return;
        end
        $fatal(1,"missing frame pub=%0d target=%0d errors=%0d parser=%0d line=%0d samples=%0d",
          publications,count,dut.errors,dut.parser,dut.line_number,dut.sample_index);
    endtask
    task check_frame(input integer slot,input integer d);
        reg [15:0] expected,actual;
        integer line_words,base;
        line_words=8+(6144>>d);base=slot*524288;
        for(integer l=0;l<64;l=l+1)
            for(integer n=0;n<line_words;n=n+1) begin
                case(n)
                    0:expected=16'h1c00;1:expected=6144;2:expected=l;3:expected=d;
                    4,5:expected=0;6:expected=16'h1234;7:expected=16'h5678;
                    default:expected=(l*17+n-8)^16'h1234;
                endcase
                actual=memory[(base+l*line_words+n)/4][((base+l*line_words+n)%4)*16+:16];
                if(actual!==expected) $fatal(1,"bad bytes slot=%0d d=%0d line=%0d word=%0d got=%h expected=%h",
                    slot,d,l,n,actual,expected);
            end
    endtask
    integer held_slot,held_token,held_frame,old_publications;
    initial begin
        repeat(8) @(negedge clk_axi);rst_n=1;
        repeat(80) @(negedge clk_in);
        command(1);
        if(dut.result!=0 || !dut.initialized) $fatal(1,"init handshake");
        for(integer d=0;d<5;d=d+1) begin
            send_frame(d);await_publication(d+1);
            write_reg(44,0);command(2);
            if(dut.result!=0 || dut.desc_bytes!=((6144>>d)+8)*128 || dut.desc_frame!=d+1) $fatal(1,"descriptor/latest frame");
            check_frame(dut.desc_slot,d);
            write_reg(48,dut.desc_lease+1);command(3);
            if(dut.result!=3 || !dut.have_lease) $fatal(1,"stale release");
            write_reg(48,dut.desc_lease);command(3);
            if(dut.result!=0 || dut.have_lease) $fatal(1,"release");
        end
        if(short_bursts!=5) $fatal(1,"missing 1024B final bursts: %0d",short_bursts);
        command(2);held_slot=dut.desc_slot;held_token=dut.desc_lease;held_frame=dut.desc_frame;
        send_frame(4);await_publication(6);send_frame(4);await_publication(7);
        send_frame(4);await_publication(8);check_frame(held_slot,4);
        if(dut.slot_state[held_slot]!=3) $fatal(1,"lease lost");
        write_reg(48,held_token);command(3);
        command(2);write_reg(44,dut.desc_frame);write_reg(48,dut.desc_lease);command(3);
        command(2);if(dut.result!=1) $fatal(1,"continuous duplicate");
        write_reg(44,0);command(2);if(dut.result!=0) $fatal(1,"stopped replay");
        write_reg(48,dut.desc_lease);command(3);
        // Invalid/missing/extra line and AXI failure must not publish.
        old_publications=publications;
        send_line(0,4);send_line(2,4);
        repeat(1000) @(negedge clk_axi);
        if(publications!=old_publications) $fatal(1,"bad sequence published");
        inject_error=1;send_frame(4);inject_error=0;
        repeat(1000) @(negedge clk_axi);
        if(publications!=old_publications) $fatal(1,"AXI error published");
        send_frame(4);await_publication(old_publications+1);
        // FIFO overflow while AW stalled: never retract AW; recover after drain.
        block_axi=1;send_line(0,0);send_line(1,0);block_axi=0;
        repeat(4000) @(negedge clk_axi);
        if(dut.errors==0) $fatal(1,"overflow not counted");
        old_publications=publications;send_frame(4);await_publication(old_publications+1);
        command(4);if(!dut.paused || dut.result) $fatal(1,"configuration drain");
        command(5);if(dut.paused) $fatal(1,"resume");
        // A truncated frame times out, is invalidated, then next SOF recovers.
        old_publications=publications;send_line(0,4);
        repeat(3100000) @(negedge clk_axi);
        if(publications!=old_publications || dut.active) $fatal(1,"incomplete frame timeout");
        send_frame(4);await_publication(old_publications+1);
        rst_n=0;repeat(10) @(negedge clk_axi);rst_n=1;
        repeat(100) @(negedge clk_in);
        command_seq=0;command(2);if(dut.result==0) $fatal(1,"reset needs init");
        command(1);if(!dut.initialized) $fatal(1,"reset handshake");
        $display("PASS ddr_frame_store five lengths, byte equality, AXI stalls, short bursts, leases, CRC-source ownership, reclaim, overflow, reset");
        $finish;
    end
    initial begin #200000000; $fatal(1,"watchdog");end
endmodule

