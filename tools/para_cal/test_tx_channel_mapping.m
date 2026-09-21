function test_tx_channel_mapping()
%TEST_TX_CHANNEL_MAPPING Self-check the TX mapping, packing and Vitis table.

channel_map = load_probe_channel_map();

% Unique, asymmetric values expose every permutation in both TX devices.
physical_delay = int32(1:64);
packed = pack_tx7332_delay_words(physical_delay, channel_map);
assert(numel(packed) == 256, ...
    'One angle must produce two 128-word synthetic-aperture transactions.');

expected_hardware = uint32(map_physical_to_tx_hardware( ...
    uint32(physical_delay), channel_map));
decoded_first = decode_delay_transaction(packed(1:128));
decoded_second = decode_delay_transaction(packed(129:256));
assert(isequal(decoded_first, expected_hardware), ...
    'First-aperture TX delay mapping is incorrect.');
assert(isequal(decoded_second, expected_hardware), ...
    'Second-aperture TX delay mapping is incorrect.');

assert(decoded_first(2) == 1 && decoded_first(1) == 2, ...
    'Physical elements 1/2 did not map to TX channels 2/1.');
assert(decoded_first(32) == 31 && decoded_first(31) == 32, ...
    'Physical elements 31/32 did not map to TX channels 32/31.');
assert(decoded_first(34) == 33 && decoded_first(33) == 34, ...
    'Physical elements 33/34 did not map to TX channels 34/33.');
assert(decoded_first(64) == 63 && decoded_first(63) == 64, ...
    'Physical elements 63/64 did not map to TX channels 64/63.');

% Verify every possible sparse element mask through both non-linear registers.
tr_bit_channels = [17:32, 1:16];
pulser_bit_channels = [17:2:31, 18:2:32, 1:2:15, 2:2:16];
for physical = 1:64
    tr_enable = true(1, 64);
    pulser_enable = true(1, 64);
    tr_enable(physical) = false;
    pulser_enable(physical) = false;
    [tr_regs, pulser_regs] = pack_tx7332_channel_enable( ...
        tr_enable, pulser_enable, channel_map);

    device = channel_map.tx_device(physical);
    local_channel = channel_map.tx_local_channel(physical);
    expected_tr_bit = find(tr_bit_channels == local_channel, 1) - 1;
    expected_pulser_bit = find(pulser_bit_channels == local_channel, 1) - 1;
    assert(bitget(tr_regs(device), expected_tr_bit + 1) == 1, ...
        'Physical element %d has an incorrect 1Ah bit.', physical);
    assert(bitget(pulser_regs(device), expected_pulser_bit + 1) == 1, ...
        'Physical element %d has an incorrect 1Bh bit.', physical);
    tr_bit_count = sum(bitget(tr_regs(1), 1:32)) + ...
                   sum(bitget(tr_regs(2), 1:32));
    pulser_bit_count = sum(bitget(pulser_regs(1), 1:32)) + ...
                       sum(bitget(pulser_regs(2), 1:32));
    assert(tr_bit_count == 1, ...
        'Physical element %d set more than one 1Ah bit.', physical);
    assert(pulser_bit_count == 1, ...
        'Physical element %d set more than one 1Bh bit.', physical);
end

% The checked-in Vitis table must exactly match the current physical model.
expected_profile = para_transmit_delay([], false);
assert(numel(expected_profile) == 16384, ...
    '64 angles x 2 apertures x 128 words must equal 16384 words.');
this_dir = fileparts(mfilename('fullpath'));
project_root = fileparts(fileparts(this_dir));
c_path = fullfile(project_root, 'build', 'src', 'ps', ...
    'bram', 'delay_profile.c');
c_text = fileread(c_path);
tokens = regexp(c_text, '0x([0-9A-Fa-f]{8})', 'tokens');
actual_profile = uint32(cellfun(@(token) hex2dec(token{1}), tokens));
assert(numel(actual_profile) == 16384, ...
    'The checked-in Vitis delay_profile.c must contain 16384 words.');
assert(isequal(actual_profile, expected_profile), ...
    'The checked-in Vitis delay_profile.c is stale.');

fprintf('PASS: TX channel map, delay packing, 1Ah/1Bh masks, and Vitis table\n');
end

function hardware_delay = decode_delay_transaction(words)
grp1_hreg = [16, 12, 8, 4, 15, 11, 7, 3];
grp1_lreg = [14, 10, 6, 2, 13, 9, 5, 1];
grp2_hreg = [32, 28, 24, 20, 31, 27, 23, 19];
grp2_lreg = [30, 26, 22, 18, 29, 25, 21, 17];
hardware_delay = zeros(1, 64, 'uint32');

for register_index = 1:8
    base = (register_index - 1) * 8;
    [high_delay, low_delay] = unpack_pair(words(base + 2));
    hardware_delay(grp1_hreg(register_index)) = high_delay;
    hardware_delay(grp1_lreg(register_index)) = low_delay;
    [high_delay, low_delay] = unpack_pair(words(base + 4));
    hardware_delay(grp2_hreg(register_index)) = high_delay;
    hardware_delay(grp2_lreg(register_index)) = low_delay;
    [high_delay, low_delay] = unpack_pair(words(base + 6));
    hardware_delay(32 + grp1_hreg(register_index)) = high_delay;
    hardware_delay(32 + grp1_lreg(register_index)) = low_delay;
    [high_delay, low_delay] = unpack_pair(words(base + 8));
    hardware_delay(32 + grp2_hreg(register_index)) = high_delay;
    hardware_delay(32 + grp2_lreg(register_index)) = low_delay;
end
end

function [high_delay, low_delay] = unpack_pair(word)
high_delay = bitand(bitshift(word, -16), uint32(65535));
low_delay = bitand(word, uint32(65535));
end

