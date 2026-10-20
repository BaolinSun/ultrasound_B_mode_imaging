// Frame-coherent DDR writer. Parameter/AXI control is clk_axi; input is clk_in.
// CDC: only the XPM asynchronous FIFO and marked single-bit synchronizers cross.
// Payload remains the original 8-word line descriptor plus decimated samples.
module ddr_frame_store #(
    parameter integer FIFO_DEPTH=8192,
    parameter integer FRAME_TIMEOUT_CYCLES=200000000
)(
    input wire clk_axi, input wire rst_n,
    input wire clk_in, input wire [15:0] in_data,
    input wire in_valid, input wire in_header, input wire in_last, input wire in_busy,
    input wire [23:0] ctrl_addr, input wire [31:0] ctrl_wdata,
    input wire ctrl_en, input wire [3:0] ctrl_we,
    output reg [31:0] ctrl_rdata,
    output wire [31:0] awaddr, output wire [7:0] awlen,
    output wire awvalid, input wire awready,
    output wire [63:0] wdata, output wire wvalid, input wire wready,
    output wire wlast, input wire bvalid, input wire [1:0] bresp,
    output wire bready, output reg frame_done
);
    localparam [23:0] BASE=24'h4c0200;
    localparam [1:0] FREE=0, WRITING=1, READY=2, READING=3;
    localparam [2:0] SEARCH=0, HEADER=1, CHECK=2, HEAD0=3, HEAD1=4, DATA=5, EOL=6;
    localparam [1:0] AX_IDLE=0, AX_ADDR=1, AX_DATA=2, AX_RESP=3;
    reg [1:0] slot_state[0:2];
    reg slot_consumed[0:2];
    reg [31:0] published;
    reg [31:0] slot_frame[0:2], slot_bytes[0:2];
    reg [3:0] slot_down[0:2];
    reg [31:0] frame_counter, drops, errors, lease_counter, lease_token;
    reg [1:0] write_slot, read_slot;
    reg have_lease, initialized, paused;
    reg [31:0] op, seq, ack, result, watermark, release_token;
    reg [31:0] desc_slot, desc_frame, desc_bytes, desc_down, desc_lease;
    reg [31:0] bytes_written, frame_bytes, frame_timer;
    reg [3:0] down;
    reg active, final_burst, abort_pending;
    reg [6:0] line_number;
    reg [12:0] samples, sample_index;
    reg [2:0] parser, hindex;
    reg [15:0] header[0:7];
    reg [1:0] word_index;
    reg [63:0] pack_word;
    (* ram_style="distributed" *) reg [63:0] burst_mem[0:255];
    reg [8:0] buffered, burst_count;
    reg [7:0] beat;
    reg [1:0] axi_state;
    reg [31:0] burst_addr;
    reg [31:0] pending_seq;
    reg [2:0] maintenance; // 0 none, 1 drain, 2 request flush, 3 wait reset
    reg maintenance_init, maintenance_purge;
    reg flush_request;
    (* ASYNC_REG="TRUE", SHREG_EXTRACT="NO" *) reg req_sync1,req_sync2;
    (* ASYNC_REG="TRUE", SHREG_EXTRACT="NO" *) reg ack_sync1,ack_sync2;
    (* ASYNC_REG="TRUE", SHREG_EXTRACT="NO" *) reg ov_sync1,ov_sync2;
    (* ASYNC_REG="TRUE", SHREG_EXTRACT="NO" *) reg busy_sync1,busy_sync2;
    reg flush_ack, flush_seen, overflow_latch;
    reg [5:0] reset_count;
    reg fifo_reset;
    // Async assertion, synchronous release in the input domain.
    (* ASYNC_REG="TRUE", SHREG_EXTRACT="NO" *) reg [1:0] rst_in_sync;
    always_ff @(posedge clk_in or negedge rst_n) begin
        if(!rst_n) rst_in_sync<=0;
        else rst_in_sync<={rst_in_sync[0],1'b1};
    end
    wire rst_in_n=rst_in_sync[1];
    wire full,empty,wr_busy,rd_busy;
    wire [18:0] fifo_word;
    wire fifo_read;
    wire fifo_write=(in_valid || in_last) && !full && !wr_busy && !fifo_reset && !overflow_latch;
    // The reset request is held until its acknowledgement. The input domain
    // resets BOTH XPM pointers, then acknowledges only after wr_rst_busy clears.
    always_ff @(posedge clk_in or negedge rst_in_n) begin
        if(!rst_in_n) begin
            req_sync1<=0;req_sync2<=0;flush_seen<=0;flush_ack<=0;
            fifo_reset<=1;reset_count<=32;overflow_latch<=0;
        end else begin
            req_sync1<=flush_request;req_sync2<=req_sync1;
            if(req_sync2!=flush_seen) begin
                flush_seen<=req_sync2;fifo_reset<=1;reset_count<=32;
                overflow_latch<=0;
            end else if(reset_count!=0) begin
                reset_count<=reset_count-1'b1;fifo_reset<=1;
            end else begin
                fifo_reset<=0;
                if(!wr_busy) flush_ack<=flush_seen;
            end
            if((in_valid || in_last) && full && !fifo_reset) overflow_latch<=1;
        end
    end
    xpm_fifo_async #(
        .FIFO_MEMORY_TYPE("block"),.FIFO_WRITE_DEPTH(FIFO_DEPTH),
        .WRITE_DATA_WIDTH(19),.READ_DATA_WIDTH(19),
        .READ_MODE("fwft"),.FIFO_READ_LATENCY(0),.CDC_SYNC_STAGES(2),
        .DOUT_RESET_VALUE("0"),.ECC_MODE("no_ecc"),.RELATED_CLOCKS(0),
        .USE_ADV_FEATURES("0000"),.SIM_ASSERT_CHK(1),
        .WR_DATA_COUNT_WIDTH($clog2(FIFO_DEPTH)+1),
        .RD_DATA_COUNT_WIDTH($clog2(FIFO_DEPTH)+1),
        .PROG_EMPTY_THRESH(16),.PROG_FULL_THRESH(16),.WAKEUP_TIME(0)
    ) input_fifo (
        .rst(fifo_reset),.wr_clk(clk_in),.wr_en(fifo_write),
        .din({in_last,in_valid,in_header,in_data}),.full(full),.wr_rst_busy(wr_busy),
        .rd_clk(clk_axi),.rd_en(fifo_read),.dout(fifo_word),.empty(empty),
        .rd_rst_busy(rd_busy),.sleep(1'b0),.injectsbiterr(1'b0),.injectdbiterr(1'b0)
    );
    always_ff @(posedge clk_axi or negedge rst_n) begin
        if(!rst_n) begin
            ack_sync1<=0;ack_sync2<=0;ov_sync1<=0;ov_sync2<=0;
            busy_sync1<=0;busy_sync2<=0;
        end else begin
            ack_sync1<=flush_ack;ack_sync2<=ack_sync1;
            ov_sync1<=overflow_latch;ov_sync2<=ov_sync1;
            busy_sync1<=in_busy;busy_sync2<=busy_sync1;
        end
    end
    wire issuing=(axi_state!=AX_IDLE);
    wire command_pending=(seq!=ack) && !maintenance;
    // Passive read-only diagnostics. No signal here feeds the writer, reset,
    // lease or original flush handshake. Input flags cross as one coherent
    // XPM held-data handshake, never as a directly sampled multi-bit bus.
    wire [15:0] diag_source={reset_count,rst_in_n,overflow_latch,full,
        wr_busy,fifo_reset,flush_ack,flush_seen,req_sync2,1'b0,1'b1};
    wire [15:0] diag_transferred;
    wire diag_received,diag_source_ack;
    wire diag_send;
    reg [15:0] diag_input;
    reg [31:0] diag_samples,diag_age;
    // XPM handshake has configuration/GSR initialization but no local reset.
    // Keep its request controller in the SAME reset epoch: a local FIFO/global
    // logic reset must not drop an in-flight request before XPM acknowledges it.
    // FPGA primitive INIT replaces a synthesizable initial block; no ASIC init.
    FDRE #(.INIT(1'b0)) diag_send_ff (
        .C(clk_in),.CE(1'b1),.R(1'b0),.Q(diag_send),
        .D((!diag_send && !diag_source_ack) ? 1'b1 :
           ((diag_send && diag_source_ack) ? 1'b0 : diag_send))
    );
    xpm_cdc_handshake #(
        .DEST_EXT_HSK(0),.DEST_SYNC_FF(2),.SRC_SYNC_FF(2),
        .INIT_SYNC_FF(1),.SIM_ASSERT_CHK(1),.WIDTH(16)
    ) diag_cdc (
        .src_clk(clk_in),.src_in(diag_source),.src_send(diag_send),
        .src_rcv(diag_source_ack),.dest_clk(clk_axi),
        .dest_out(diag_transferred),.dest_req(diag_received),.dest_ack(1'b0)
    );
    always_ff @(posedge clk_axi or negedge rst_n) begin
        if(!rst_n) begin diag_input<=0;diag_samples<=0;diag_age<=0;end
        else if(diag_received) begin
            diag_input<=diag_transferred;diag_samples<=diag_samples+1'b1;diag_age<=0;
        end else if(diag_age!=32'hffffffff) diag_age<=diag_age+1'b1;
    end
    wire can_parse=initialized && maintenance<=1 && !abort_pending && !ov_sync2 &&
                   !rd_busy && !empty && !issuing && buffered<256 &&
                   !command_pending;
    assign fifo_read=can_parse && (parser==SEARCH || parser==HEADER || parser==DATA || parser==EOL);
    assign awaddr=burst_addr;
    assign awlen=burst_count[7:0]-1'b1;
    assign awvalid=(axi_state==AX_ADDR);
    assign wdata=burst_mem[beat];
    assign wvalid=(axi_state==AX_DATA);
    assign wlast=(beat==burst_count-1'b1);
    assign bready=(axi_state==AX_RESP);

    // Separate reset-free single-write RAM process: resets invalidate counters,
    // never reset sample storage. This infers LUT RAM rather than 16384 FFs.
    wire ram_step=initialized && maintenance<=1 && !issuing && !abort_pending &&
                  !ov_sync2 && !command_pending && buffered<256;
    always_ff @(posedge clk_axi) begin
        if(rst_n && ram_step) begin
            case(parser)
                HEAD0:burst_mem[buffered[7:0]]<={header[3],header[2],header[1],header[0]};
                HEAD1:burst_mem[buffered[7:0]]<={header[7],header[6],header[5],header[4]};
                DATA:if(can_parse && fifo_word[17] && !fifo_word[16] &&
                        (!fifo_word[18] || sample_index==samples-1'b1) && word_index==3)
                    burst_mem[buffered[7:0]]<={fifo_word[15:0],pack_word[47:0]};
                default:begin end
            endcase
        end
    end
    integer i, candidate, newest, acquire_candidate;
    reg [31:0] latest;
    function automatic newer(input [31:0] a,input [31:0] b);
        newer=($signed(a-b)>0);
    endfunction
    // Parallel pairwise comparisons: never chain selected frame-ID muxes into
    // another 32-bit subtractor. Valid IDs differ by much less than 2^31.
    wire n01=newer(slot_frame[0],slot_frame[1]);
    wire n02=newer(slot_frame[0],slot_frame[2]);
    wire n12=newer(slot_frame[1],slot_frame[2]);
    wire [2:0] valid_slot={slot_state[2]>=READY,slot_state[1]>=READY,slot_state[0]>=READY};
    wire [2:0] ready_slot={slot_state[2]==READY,slot_state[1]==READY,slot_state[0]==READY};
    wire [2:0] eligible={
        ready_slot[2] && (watermark==0 || newer(slot_frame[2],watermark)),
        ready_slot[1] && (watermark==0 || newer(slot_frame[1],watermark)),
        ready_slot[0] && (watermark==0 || newer(slot_frame[0],watermark))};
    always_comb begin
        newest=-1;candidate=-1;acquire_candidate=-1;latest=0;
        if(valid_slot[0] && (!valid_slot[1] || n01) && (!valid_slot[2] || n02)) newest=0;
        else if(valid_slot[1] && (!valid_slot[2] || n12)) newest=1;
        else if(valid_slot[2]) newest=2;
        if(newest>=0) latest=slot_frame[newest];
        if(eligible[0] && (!eligible[1] || n01) && (!eligible[2] || n02)) acquire_candidate=0;
        else if(eligible[1] && (!eligible[2] || n12)) acquire_candidate=1;
        else if(eligible[2]) acquire_candidate=2;

        if(slot_state[0]==FREE) candidate=0;
        else if(slot_state[1]==FREE) candidate=1;
        else if(slot_state[2]==FREE) candidate=2;
        else if(ready_slot[0] && (!ready_slot[1] || !n01) && (!ready_slot[2] || !n02)) candidate=0;
        else if(ready_slot[1] && (!ready_slot[2] || !n12)) candidate=1;
        else if(ready_slot[2]) candidate=2;
        ctrl_rdata=0;
        case(ctrl_addr-BASE)
            0:ctrl_rdata=32'h55424631;
            12:ctrl_rdata=ack;
            16:ctrl_rdata=result;
            20:ctrl_rdata={28'd0,paused,busy_sync2,active|issuing,initialized};
            24:ctrl_rdata=desc_slot;
            28:ctrl_rdata=desc_frame;
            32:ctrl_rdata=desc_bytes;
            36:ctrl_rdata=desc_down;
            40:ctrl_rdata=desc_lease;
            52:ctrl_rdata=drops;
            56:ctrl_rdata=errors;
            60:ctrl_rdata={26'd0,slot_state[2],slot_state[1],slot_state[0]};
            64:ctrl_rdata=latest;
            68:ctrl_rdata=published;
            72:ctrl_rdata=32'h55424431; // UBD1 read-only diagnostic ABI
            76:ctrl_rdata={14'd0,maintenance_purge,maintenance_init,initialized,
                paused,empty,command_pending,abort_pending,active,issuing,
                busy_sync2,ov_sync2,rd_busy,ack_sync2,1'b0,flush_request,maintenance};
            80:ctrl_rdata=seq;
            84:ctrl_rdata=op;
            88:ctrl_rdata=pending_seq;
            92:ctrl_rdata={16'd0,diag_input};
            96:ctrl_rdata=diag_samples;
            100:ctrl_rdata=diag_age; // clk_axi cycles, saturates; detects stale source
            104:ctrl_rdata={27'd0,axi_state,parser};
            default:ctrl_rdata=0;
        endcase
    end
    wire [15:0] hdr_line=header[2] & 16'h007f;
    wire [3:0] hdr_down=header[3][3:0];
    wire header_ok=header[0]==16'h1c00 && header[1]==6144 &&
                   hdr_line<64 && hdr_down<=4;
    // One sequential owner arbitrates slot publication, leases and reclamation.
    // Commands have priority over parser progress, making acquire vs SOF atomic.
    always_ff @(posedge clk_axi or negedge rst_n) begin
        if(!rst_n) begin
            for(i=0;i<3;i=i+1) begin
                slot_state[i]<=FREE;slot_frame[i]<=0;slot_bytes[i]<=0;slot_down[i]<=0;
                slot_consumed[i]<=0;
            end
            for(i=0;i<8;i=i+1) header[i]<=0;
            op<=0;seq<=0;ack<=0;result<=0;watermark<=0;release_token<=0;
            initialized<=0;paused<=0;have_lease<=0;lease_token<=0;lease_counter<=0;
            desc_slot<=0;desc_frame<=0;desc_bytes<=0;desc_down<=0;desc_lease<=0;
            write_slot<=0;read_slot<=0;frame_counter<=0;drops<=0;errors<=0;
            published<=0;
            bytes_written<=0;frame_bytes<=0;frame_timer<=0;down<=0;active<=0;
            parser<=SEARCH;hindex<=0;line_number<=0;samples<=0;sample_index<=0;
            buffered<=0;pack_word<=0;word_index<=0;
            axi_state<=AX_IDLE;burst_count<=0;beat<=0;burst_addr<=0;
            final_burst<=0;abort_pending<=0;frame_done<=0;
            maintenance<=0;maintenance_init<=0;maintenance_purge<=0;pending_seq<=0;flush_request<=0;
        end else begin
            frame_done<=0;
            if(ctrl_en && &ctrl_we && ctrl_addr[23:8]==BASE[23:8]) begin
                case(ctrl_addr-BASE)
                    4:op<=ctrl_wdata;
                    8:seq<=ctrl_wdata;
                    44:watermark<=ctrl_wdata;
                    48:release_token<=ctrl_wdata;
                    default:begin end
                endcase
            end
            if(active) begin
                if(frame_timer<FRAME_TIMEOUT_CYCLES) frame_timer<=frame_timer+1'b1;
                else abort_pending<=1;
            end
            if(ov_sync2 && initialized && maintenance!=3) abort_pending<=1;
            // AXI address/data are independently held until accepted. We issue
            // data after AW, so there is at most one outstanding burst/response.
            case(axi_state)
                AX_ADDR:if(awready) axi_state<=AX_DATA;
                AX_DATA:if(wready) begin
                    if(wlast) axi_state<=AX_RESP;
                    else beat<=beat+1'b1;
                end
                AX_RESP:if(bvalid) begin
                    axi_state<=AX_IDLE;buffered<=0;bytes_written<=bytes_written+{20'd0,burst_count,3'd0};
                    if(bresp!=0) abort_pending<=1;
                    else if(final_burst && !abort_pending && !ov_sync2 &&
                            frame_timer<FRAME_TIMEOUT_CYCLES) begin
                        if(bytes_written+{20'd0,burst_count,3'd0}==frame_bytes) begin
                            slot_state[write_slot]<=READY;
                            slot_bytes[write_slot]<=frame_bytes;
                            slot_down[write_slot]<=down;
                            slot_frame[write_slot]<=frame_counter;
                            slot_consumed[write_slot]<=0;published<=published+1'b1;
                            frame_done<=1;active<=0;
                        end else abort_pending<=1;
                    end
                    final_burst<=0;
                end
                default:begin end
            endcase

            if(command_pending && !abort_pending && !ov_sync2) begin
                ack<=seq;result<=0;
                case(op)
                    1,4:begin // INIT / PAUSE: await actual input, parser and AXI drain.
                        pending_seq<=seq;ack<=ack;maintenance<=1;maintenance_init<=(op==1);maintenance_purge<=1;
                        paused<=1;
                    end
                    2:begin
                        if(!initialized || have_lease) result<=2;
                        else if(acquire_candidate<0) result<=1;
                        else begin
                            have_lease<=1;read_slot<=acquire_candidate[1:0];
                            lease_counter<=lease_counter==32'hffffffff ? 1:lease_counter+1'b1;
                            lease_token<=lease_counter==32'hffffffff ? 1:lease_counter+1'b1;
                            desc_lease<=lease_counter==32'hffffffff ? 1:lease_counter+1'b1;
                            desc_slot<=acquire_candidate;desc_frame<=slot_frame[acquire_candidate];
                            desc_bytes<=slot_bytes[acquire_candidate];desc_down<={28'd0,slot_down[acquire_candidate]};
                            slot_state[acquire_candidate]<=READING;
                            slot_consumed[acquire_candidate]<=1;
                        end
                    end
                    3:if(have_lease && release_token==lease_token) begin
                        slot_state[read_slot]<=paused ? FREE:READY;have_lease<=0;
                    end else result<=3;
                    5:if(initialized && !maintenance) paused<=0; else result<=3;
                    default:result<=3;
                endcase
            end
            // PAUSE drains the current frame rather than cancelling AXI. Once
            // scan and processing report idle, discard queued old-format words.
            if(maintenance==1 && !busy_sync2 && !active && !issuing) maintenance<=2;
            if(maintenance==2 && !issuing) begin
                flush_request<=!flush_request;maintenance<=3;parser<=SEARCH;buffered<=0;word_index<=0;
            end
            if(maintenance==3 && ack_sync2==flush_request && !rd_busy && !ov_sync2) begin
                maintenance<=0;ack<=pending_seq;result<=0;
                if(maintenance_purge) initialized<=1;
                if(maintenance_purge)
                    for(i=0;i<3;i=i+1) if(slot_state[i]!=READING || maintenance_init) slot_state[i]<=FREE;
                if(maintenance_init) begin have_lease<=0;paused<=0;end
            end

            // Do not cancel AW/W/B on errors. Clear ownership only after B.
            if(abort_pending && !issuing && maintenance!=3) begin
                if(active) begin slot_state[write_slot]<=FREE;drops<=drops+1'b1;end
                errors<=errors+1'b1;active<=0;buffered<=0;word_index<=0;parser<=SEARCH;
                // Reuse the same dual-domain reset handshake, not a unilateral
                // read-pointer reset. Retain any valid PS lease and READY frames.
                flush_request<=!flush_request;maintenance<=3;pending_seq<=ack;
                maintenance_init<=0;maintenance_purge<=0;abort_pending<=0;
            end

            if(!issuing && !abort_pending && !ov_sync2 && active &&
                (buffered==256 || final_burst)) begin
                if(buffered!=0) begin
                    burst_count<=buffered;beat<=0;
                    burst_addr<=32'h20000000+{10'd0,write_slot,20'd0}+bytes_written;
                    axi_state<=AX_ADDR;
                end
            end else if(can_parse || (initialized && maintenance<=1 && !issuing &&
                        !abort_pending && !ov_sync2 && !command_pending && buffered<256 &&
                        (parser==CHECK || parser==HEAD0 || parser==HEAD1))) begin
                case(parser)
                    SEARCH:if(fifo_word[17] && fifo_word[16]) begin
                        header[0]<=fifo_word[15:0];hindex<=1;parser<=HEADER;
                    end else if(active) abort_pending<=1;
                    HEADER:begin
                        if(!fifo_word[17] || !fifo_word[16] || fifo_word[18]) begin parser<=SEARCH;if(active) abort_pending<=1;end
                        else begin
                            header[hindex]<=fifo_word[15:0];
                            if(hindex==7) parser<=CHECK;else hindex<=hindex+1'b1;
                        end
                    end
                    CHECK:begin
                        if(!header_ok || (active && (hdr_line!=line_number || hdr_down!=down))) begin
                            parser<=SEARCH;if(active) abort_pending<=1;
                        end else if(!active) begin
                            if(hdr_line==0 && !paused) begin
                                frame_counter<=frame_counter==32'hffffffff ? 1:frame_counter+1'b1;
                                if(candidate<0) begin drops<=drops+1'b1;parser<=SEARCH;end
                                else begin
                                    if(slot_state[candidate]==READY && !slot_consumed[candidate]) drops<=drops+1'b1;
                                    write_slot<=candidate[1:0];slot_state[candidate]<=WRITING;
                                    active<=1;down<=hdr_down;frame_timer<=0;bytes_written<=0;
                                    samples<=13'd6144 >> hdr_down;
                                    frame_bytes<=((32'd6144 >> hdr_down)+8)*128;
                                    line_number<=0;sample_index<=0;parser<=HEAD0;
                                end
                            end else parser<=SEARCH;
                        end else begin sample_index<=0;parser<=HEAD0;end
                    end
                    HEAD0:begin
                        buffered<=buffered+1'b1;parser<=HEAD1;
                    end
                    HEAD1:begin
                        buffered<=buffered+1'b1;word_index<=0;parser<=DATA;
                    end
                    DATA:begin
                        if(!fifo_word[17] || fifo_word[16] || (fifo_word[18] && sample_index!=samples-1'b1)) begin abort_pending<=1;parser<=SEARCH;end
                        else begin
                            pack_word[word_index*16 +:16]<=fifo_word[15:0];
                            sample_index<=sample_index+1'b1;
                            if(word_index==3) begin
                                buffered<=buffered+1'b1;word_index<=0;
                            end else word_index<=word_index+1'b1;
                            if(sample_index==samples-1'b1) begin
                                parser<=EOL;
                                if(fifo_word[18]) begin
                                    parser<=SEARCH;line_number<=line_number+1'b1;
                                    if(line_number==63) final_burst<=1;
                                end
                            end
                        end
                    end
                    EOL:begin
                        if(fifo_word[17] || !fifo_word[18]) abort_pending<=1;
                        else begin
                            line_number<=line_number+1'b1;
                            if(line_number==63) final_burst<=1;
                        end
                        parser<=SEARCH;
                    end
                    default:begin parser<=SEARCH;abort_pending<=active;end
                endcase
            end
        end
    end
endmodule

