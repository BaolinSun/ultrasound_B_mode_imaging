function delay_fpga = pack_tx7332_delay_words(physical_delay, channel_map)
%PACK_TX7332_DELAY_WORDS Map physical delays and pack TX7332 BRAM words.
%
% The returned stream contains two 128-word synthetic-aperture transactions
% per transmit angle. The four address/data pairs in each transaction retain
% the existing TX1-group1, TX1-group2, TX2-group1, TX2-group2 SPI order.

if nargin < 2 || isempty(channel_map)
    channel_map = load_probe_channel_map();
end
assert(size(physical_delay, 2) == 64, ...
    'physical_delay must have exactly 64 columns in physical-element order.');

hardware_delay = map_physical_to_tx_hardware(physical_delay, channel_map);

grp1_addr = [40, 41, 42, 43, 44, 45, 46, 47];
grp1_hreg = [16, 12, 8, 4, 15, 11, 7, 3];
grp1_lreg = [14, 10, 6, 2, 13, 9, 5, 1];

grp2_addr = [32, 33, 34, 35, 36, 37, 38, 39];
grp2_hreg = [32, 28, 24, 20, 31, 27, 23, 19];
grp2_lreg = [30, 26, 22, 18, 29, 25, 21, 17];

words_per_aperture = 128;
apertures_per_angle = 2;
delay_fpga = zeros(1, size(hardware_delay, 1) * ...
    words_per_aperture * apertures_per_angle, 'uint32');
write_index = 1;
all_ones = uint32(2^32 - 1);

for angle_index = 1:size(hardware_delay, 1)
    chip1 = hardware_delay(angle_index, 1:32);
    chip2 = hardware_delay(angle_index, 33:64);

    for aperture = 0:1
        for register_index = 0:15
            if register_index < 8
                index = register_index + 1;
                words = uint32([ ...
                    grp1_addr(index), ...
                    pack_delay_pair(chip1(grp1_hreg(index)), chip1(grp1_lreg(index))), ...
                    grp2_addr(index), ...
                    pack_delay_pair(chip1(grp2_hreg(index)), chip1(grp2_lreg(index))), ...
                    grp1_addr(index), ...
                    pack_delay_pair(chip2(grp1_hreg(index)), chip2(grp1_lreg(index))), ...
                    grp2_addr(index), ...
                    pack_delay_pair(chip2(grp2_hreg(index)), chip2(grp2_lreg(index))) ...
                    ]);
            elseif register_index == 8
                if aperture == 0
                    words = uint32([26, 0, 26, 0, ...
                                    26, all_ones, 26, all_ones]);
                else
                    words = uint32([26, all_ones, 26, all_ones, ...
                                    26, 0, 26, 0]);
                end
            else
                words = zeros(1, 8, 'uint32');
            end

            if register_index == 15
                words(end) = uint32(9);
            end
            delay_fpga(write_index:write_index+7) = words;
            write_index = write_index + 8;
        end
    end
end

assert(write_index == numel(delay_fpga) + 1, ...
    'Internal error while packing the TX7332 delay stream.');
end

function word = pack_delay_pair(high_delay, low_delay)
high_word = uint32(mod(double(high_delay), 2^16));
low_word = uint32(mod(double(low_delay), 2^16));
word = bitor(bitshift(high_word, 16), low_word);
end

