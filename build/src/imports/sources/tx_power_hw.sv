// Independent TX STDBY. clk_ctrl never depends on AFE DCLK.
// CDC: req/ack are 2-FF toggles; payload/report are held for the entire handshake.
module tx_power_hw (
    input logic clk_ctrl, clk_tx, rst_n,
    input logic [23:0] addr,
    input logic [31:0] wdata,
    input logic en,
    input logic [3:0] we,
    output logic [31:0] rdata,
    input logic tx_idle, ctrl_ready,
    output logic tx_hold, tx_standby
);
    logic [31:0] control, cmd_seq, ack_cmd_seq, report_cmd_seq;
    logic [1:0] payload_control, tx_control;
    logic req, ack_tx;
    (* ASYNC_REG="TRUE", SHREG_EXTRACT="NO" *) logic req_sync1,req_sync2,ack_sync1,ack_sync2;
    (* ASYNC_REG="TRUE", SHREG_EXTRACT="NO" *) logic idle_sync1,idle_sync2;
    logic [2:0] report_flags, flags;
    logic busy, tx_pending;
    logic report_error, error_tx;
    logic idle_tx_q;
    logic seen_tx;
    logic [3:0] quiet_count;
    wire selected=addr[23:6]==18'h1300d;
    always_comb begin
        rdata=0;
        case(addr[5:0])
            0:rdata=32'h55545831;
            4:rdata=control;
            8:rdata=cmd_seq;
            12:rdata=ack_cmd_seq;
            16:rdata={26'd0,busy,report_error,flags[2],flags[1],flags[0],idle_sync2};
            default:rdata=0;
        endcase
        // flags are a bundled {ctrl_ready, standby, hold} snapshot.
    end
    always_ff @(posedge clk_ctrl) begin
        if(!rst_n) begin
            control<=0;cmd_seq<=0;ack_cmd_seq<=0;payload_control<=0;
            req<=0;ack_sync1<=0;ack_sync2<=0;busy<=0;flags<=0;report_error<=0;
            idle_sync1<=0;idle_sync2<=0;
        end else begin
            ack_sync1<=ack_tx;ack_sync2<=ack_sync1;
            idle_sync1<=idle_tx_q;idle_sync2<=idle_sync1;
            if(en && selected && &we && !busy) begin
                if(addr[5:0]==4) control<=wdata;
                if(addr[5:0]==8 && wdata!=0 && wdata!=cmd_seq) begin
                    cmd_seq<=wdata;payload_control<=control[1:0];
                    req<=!req;busy<=1;
                end
            end
            if(busy && ack_sync2==req) begin
                ack_cmd_seq<=report_cmd_seq;flags<=report_flags;report_error<=error_tx;busy<=0;
            end
        end
    end
    // A command may wait for idle, but cannot reset/cancel an accepted SPI.
    // Eight consecutive quiet cycles cover TX request/trigger pipelines.
    always_ff @(posedge clk_tx) begin
        if(!rst_n) begin
            req_sync1<=0;req_sync2<=0;seen_tx<=0;ack_tx<=0;tx_pending<=0;tx_control<=0;
            tx_hold<=0;tx_standby<=0;report_cmd_seq<=0;report_flags<=0;error_tx<=0;quiet_count<=0;idle_tx_q<=0;
        end else begin
            req_sync1<=req;req_sync2<=req_sync1;
            idle_tx_q<=tx_idle; // register the combined status before CDC (no combinational glitches)
            if(req_sync2!=seen_tx && !tx_pending) begin
                tx_pending<=1;seen_tx<=req_sync2;quiet_count<=0;
            end
            if(tx_pending) begin
                // Registered request-pending is the destination capture enable.
                // Payload was held before the 2FF request and remains held through ACK.
                tx_control<=payload_control;
                if(!tx_idle || !ctrl_ready) quiet_count<=0;
                else if(quiet_count!=8) quiet_count<=quiet_count+1'b1;
                if(quiet_count==8 && tx_idle && ctrl_ready) begin
                    error_tx<=tx_control[1]&&!tx_control[0];
                    if(!(tx_control[1]&&!tx_control[0])) begin
                        tx_hold<=tx_control[0];tx_standby<=tx_control[1];
                        report_flags<={ctrl_ready,tx_control[1:0]};
                    end else report_flags<={ctrl_ready,tx_standby,tx_hold};
                    report_cmd_seq<=cmd_seq;ack_tx<=seen_tx;tx_pending<=0;
                end
            end
        end
    end
endmodule
