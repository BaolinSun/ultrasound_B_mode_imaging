// All inputs are synchronous to clk_25. scan_active and bf_window have already
// passed their two-stage synchronizers in proc_top. busy is a held level; its
// only CDC consumer is frame_store_i/busy_sync1/2 (see frame_store_cdc.xdc).
// No timer can erase accepted work: missing completions keep the drain closed.
module capture_drain_tracker #(
    parameter integer COUNT_WIDTH = 4
)(
    input  wire clk_25,
    input  wire reset,
    input  wire scan_active,
    input  wire bf_window,
    input  wire shot_done,
    input  wire line_start,
    input  wire line_done,
    input  wire output_valid,
    output logic busy,
    output logic fault
);
    logic bf_window_d, armed;
    logic [COUNT_WIDTH-1:0] shots_pending, lines_pending;
    wire shot_start = bf_window && !bf_window_d;
    localparam logic [COUNT_WIDTH-1:0] MAX_PENDING = {COUNT_WIDTH{1'b1}};

    always_ff @(posedge clk_25) begin
        if (reset) begin
            bf_window_d <= 1'b0;
            armed <= 1'b0;
            shots_pending <= '0;
            lines_pending <= '0;
            busy <= 1'b0;
            fault <= 1'b0;
        end else begin
            bf_window_d <= bf_window;
            if (shot_start) armed <= 1'b1;
            // BF window is a level, not a new transaction on every cycle.
            // Each physical shot ends at add_rf_head.trans_last. Only phase 3
            // creates a PI image line; that line ends at log_last.
            case ({shot_start, shot_done})
                2'b10: if (shots_pending != MAX_PENDING) shots_pending <= shots_pending + 1'b1;
                       else fault <= 1'b1;
                2'b01: if (shots_pending != 0) shots_pending <= shots_pending - 1'b1;
                       else if (armed) fault <= 1'b1;
                default: begin end
            endcase
            case ({line_start, line_done})
                2'b10: if (lines_pending != MAX_PENDING) lines_pending <= lines_pending + 1'b1;
                       else fault <= 1'b1;
                2'b01: if (lines_pending != 0) lines_pending <= lines_pending - 1'b1;
                       else if (armed) fault <= 1'b1;
                default: begin end
            endcase
            // Keep one final registered cycle after completion, including the
            // registered output bridge. Never drop busy in an in-flight gap.
            busy <= scan_active || bf_window || shot_start || shot_done || line_start || line_done ||
                    shots_pending != 0 || lines_pending != 0 || output_valid || fault;
        end
    end
endmodule
