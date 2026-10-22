// AFE-only control. clk_ctrl remains alive when AFE DCLK is absent.
// CDC: Gray raw clock count; 2FF levels; held-data epoch request/ACK handshake.
module afe_power_hw (
    input logic clk_ctrl, rst_n, clk_raw, clk_rx,
    input logic mmcm_locked, idelay_ready, rx_locked,
    input logic [13:0] fclk_word,
    input logic [23:0] ctrl_addr,
    input logic [31:0] ctrl_wdata,
    input logic ctrl_en, input logic [3:0] ctrl_we,
    output logic [31:0] ctrl_rdata,
    output logic mmcm_reset, rx_reset
);
    logic [1:0] control;
    (* ASYNC_REG="TRUE", SHREG_EXTRACT="NO" *) logic [1:0] rst_raw_sync,rst_rx_sync;
    always_ff @(posedge clk_raw or negedge rst_n) begin
        if(!rst_n) rst_raw_sync<=0;else rst_raw_sync<={rst_raw_sync[0],1'b1};
    end
    always_ff @(posedge clk_rx or negedge rst_n) begin
        if(!rst_n) rst_rx_sync<=0;else rst_rx_sync<={rst_rx_sync[0],1'b1};
    end
    wire raw_rst_n=rst_raw_sync[1],rx_rst_n=rst_rx_sync[1];
    logic [31:0] epoch, ack_epoch;
    logic [7:0] raw_count,raw_gray;
    logic req_toggle, report_toggle;
    logic [31:0] rx_epoch, report_epoch;
    logic seen;
    (* ASYNC_REG="TRUE", SHREG_EXTRACT="NO" *) logic [7:0] raw_sync1,raw_sync2;
    (* ASYNC_REG="TRUE", SHREG_EXTRACT="NO" *) logic req_sync1,req_sync2;
    (* ASYNC_REG="TRUE", SHREG_EXTRACT="NO" *) logic ack_sync1,ack_sync2;
    (* ASYNC_REG="TRUE", SHREG_EXTRACT="NO" *) logic ready_sync1,ready_sync2;
    (* ASYNC_REG="TRUE", SHREG_EXTRACT="NO" *) logic mmcm_sync1,mmcm_sync2,idelay_sync1,idelay_sync2;
    logic [7:0] previous_raw;
    logic [15:0] raw_age;
    logic [7:0] good;
    logic previous_half, have_half, report_ready;
    (* ASYNC_REG="TRUE", SHREG_EXTRACT="NO" *) logic [1:0] hold_sync;
    (* ASYNC_REG="TRUE", SHREG_EXTRACT="NO" *) logic lock_rx1,lock_rx2,idelay_rx1,idelay_rx2;
    wire monitor_rst_n=rx_rst_n && !rx_reset;
    always_ff @(posedge clk_rx or negedge monitor_rst_n) begin
        if(!monitor_rst_n) hold_sync<=2'b11;
        else hold_sync<={hold_sync[0],1'b0};
    end
    wire half_valid=(&fclk_word) || (~|fclk_word);
    wire half_one=&fclk_word;
    assign mmcm_reset=control[0];
    assign rx_reset=control[1];
    always_ff @(posedge clk_raw or negedge raw_rst_n) begin
        if(!raw_rst_n) begin raw_count<=0;raw_gray<=0;end
        else begin raw_count<=raw_count+1'b1;raw_gray<=((raw_count+1'b1)>>1)^(raw_count+1'b1);end
    end
    // Bus epoch is held until returned report_toggle acknowledges its request.
    // Do NOT reset this handshake on local receiver reset or loss of DCLK.
    always_ff @(posedge clk_rx or negedge rx_rst_n) begin
        if(!rx_rst_n) begin
            req_sync1<=0;req_sync2<=0;seen<=0;rx_epoch<=0;
            report_epoch<=0;report_toggle<=0;report_ready<=0;
            good<=0;previous_half<=0;have_half<=0;
            lock_rx1<=0;lock_rx2<=0;idelay_rx1<=0;idelay_rx2<=0;
        end else begin
            lock_rx1<=mmcm_locked;lock_rx2<=lock_rx1;
            idelay_rx1<=idelay_ready;idelay_rx2<=idelay_rx1;
            req_sync1<=req_toggle;req_sync2<=req_sync1;
            if(req_sync2!=seen) begin
                rx_epoch<=epoch;seen<=req_sync2;good<=0;have_half<=0;report_ready<=0;
            end else if(hold_sync[1] || !lock_rx2 || !idelay_rx2 || !rx_locked || !half_valid) begin
                good<=0;have_half<=0;report_ready<=0;
            end else begin
                previous_half<=half_one;have_half<=1;
                if(have_half && half_one!=previous_half) begin
                    if(good!=255) good<=good+1'b1;
                    if(good>=127) begin
                        report_epoch<=rx_epoch;report_toggle<=seen;report_ready<=1;
                    end
                end else begin good<=0;report_ready<=0;end
            end
        end
    end
    always_ff @(posedge clk_ctrl or negedge rst_n) begin
        if(!rst_n) begin
            control<=0;epoch<=0;req_toggle<=0;ack_epoch<=0;
            raw_sync1<=0;raw_sync2<=0;previous_raw<=0;raw_age<=16'hffff;
            ack_sync1<=0;ack_sync2<=0;ready_sync1<=0;ready_sync2<=0;
            mmcm_sync1<=0;mmcm_sync2<=0;idelay_sync1<=0;idelay_sync2<=0;
        end else begin
            raw_sync1<=raw_gray;raw_sync2<=raw_sync1;
            previous_raw<=raw_sync2;
            if(raw_sync2!=previous_raw) raw_age<=0;
            else if(raw_age!=16'hffff) raw_age<=raw_age+1'b1;
            ack_sync1<=report_toggle;ack_sync2<=ack_sync1;
            ready_sync1<=report_ready;ready_sync2<=ready_sync1;
            mmcm_sync1<=mmcm_locked;mmcm_sync2<=mmcm_sync1;
            idelay_sync1<=idelay_ready;idelay_sync2<=idelay_sync1;
            // Source data has been held through the two toggle synchronizers.
            if(ack_sync2==req_toggle && ready_sync2) ack_epoch<=report_epoch;
            if(ctrl_en && ctrl_we==4'hf) begin
                if(ctrl_addr==24'h4c0304) control<=ctrl_wdata[1:0];
                if(ctrl_addr==24'h4c0308 && control[1] &&
                   (ack_sync2==req_toggle || ctrl_wdata==epoch)) begin
                    if(ctrl_wdata!=epoch) begin epoch<=ctrl_wdata;req_toggle<=~req_toggle;end
                end
            end
        end
    end
    wire fresh=(ack_epoch==epoch) && ready_sync2 && !control[1] && !control[0];
    always_comb begin
        ctrl_rdata=0;
        case(ctrl_addr)
            24'h4c0300:ctrl_rdata=32'h55414631;
            24'h4c0304:ctrl_rdata={30'd0,control};
            24'h4c0308:ctrl_rdata=epoch;
            24'h4c030c:ctrl_rdata={27'd0,fresh,(ready_sync2 && !control[1]),
                idelay_sync2,mmcm_sync2,(raw_age<16'd4096)};
            24'h4c0310:ctrl_rdata=ack_epoch;
            24'h4c0314:ctrl_rdata={24'd0,raw_sync2}; // Gray encoded activity, not frequency
            24'h4c0318:ctrl_rdata={31'd0,ready_sync2};
            default:ctrl_rdata=0;
        endcase
    end
endmodule
