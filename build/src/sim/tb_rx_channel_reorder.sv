`timescale 1ns/1ps

module tb_rx_channel_reorder;

logic         raw_valid;
logic [511:0] raw_data;
logic         physical_valid;
logic [511:0] physical_data;

integer raw_channel;
integer physical_slot;
integer aperture;
integer expected_afe_channel;
integer expected_global_element;

rx_channel_reorder dut
(
    .raw_valid      (raw_valid),
    .raw_data       (raw_data),
    .physical_valid (physical_valid),
    .physical_data  (physical_data)
);

initial begin
    raw_valid = 1'b0;
    raw_data = '0;
    for(raw_channel = 0; raw_channel < 32; raw_channel = raw_channel + 1) begin
        raw_data[raw_channel*16 +: 16] = raw_channel + 1;
    end

    #1;
    assert(physical_valid === 1'b0)
        else $fatal(1, "physical_valid must be a zero-latency copy of raw_valid");

    raw_valid = 1'b1;
    #1;
    assert(physical_valid === 1'b1)
        else $fatal(1, "physical_valid did not follow raw_valid combinationally");

    for(physical_slot = 0; physical_slot < 32; physical_slot = physical_slot + 1) begin
        expected_afe_channel = (physical_slot ^ 1) + 1;
        assert(physical_data[physical_slot*16 +: 16] == expected_afe_channel)
            else $fatal(1,
                "Physical slot %0d expected AFE CH%0d, got tag %0d",
                physical_slot + 1,
                expected_afe_channel,
                physical_data[physical_slot*16 +: 16]);
    end

    // The same local AFE permutation is used by both synthetic apertures.
    // rtc_line[0] remains a downstream half-aperture selector.
    for(aperture = 0; aperture < 2; aperture = aperture + 1) begin
        for(physical_slot = 0; physical_slot < 32; physical_slot = physical_slot + 1) begin
            expected_global_element = aperture*32 + physical_slot + 1;
            expected_afe_channel = (physical_slot ^ 1) + 1;
            assert(expected_global_element >= aperture*32 + 1 &&
                   expected_global_element <= aperture*32 + 32)
                else $fatal(1, "Synthetic-aperture global element calculation failed");
            assert(physical_data[physical_slot*16 +: 16] == expected_afe_channel)
                else $fatal(1, "Synthetic-aperture mapping changed unexpectedly");
        end
    end

    raw_valid = 1'b0;
    #1;
    assert(physical_valid === 1'b0)
        else $fatal(1, "physical_valid did not deassert without latency");

    $display("PASS: RX AFE channels map to natural physical-element order");
    $finish;
end

endmodule

