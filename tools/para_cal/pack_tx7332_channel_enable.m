function [tr_switch_disable_reg, pulser_disable_reg] = ...
    pack_tx7332_channel_enable(physical_tr_enable, physical_pulser_enable, channel_map)
%PACK_TX7332_CHANNEL_ENABLE Pack physical enable vectors into 1Ah/1Bh.
%
% Inputs use natural physical-element order and true means enabled. Outputs
% contain one 32-bit register value per TX7332 device; set bits disable the
% corresponding hardware channel, as defined by TX7332 registers 1Ah/1Bh.

if nargin < 3 || isempty(channel_map)
    channel_map = load_probe_channel_map();
end
validate_enable_vector(physical_tr_enable, 'physical_tr_enable');
validate_enable_vector(physical_pulser_enable, 'physical_pulser_enable');

hardware_tr_enable = logical(map_physical_to_tx_hardware( ...
    logical(physical_tr_enable), channel_map));
hardware_pulser_enable = logical(map_physical_to_tx_hardware( ...
    logical(physical_pulser_enable), channel_map));

tr_switch_disable_reg = zeros(1, 2, 'uint32');
pulser_disable_reg = zeros(1, 2, 'uint32');

for device = 1:2
    device_offset = (device - 1) * 32;
    for channel = 1:32
        if ~hardware_tr_enable(device_offset + channel)
            tr_switch_disable_reg(device) = bitset( ...
                tr_switch_disable_reg(device), tr_switch_bit(channel) + 1, 1);
        end
        if ~hardware_pulser_enable(device_offset + channel)
            pulser_disable_reg(device) = bitset( ...
                pulser_disable_reg(device), pulser_bit(channel) + 1, 1);
        end
    end
end
end

function validate_enable_vector(values, name)
assert(isvector(values) && numel(values) == 64, ...
    '%s must contain exactly 64 physical-element enables.', name);
assert(all(values(:) == 0 | values(:) == 1), ...
    '%s must contain only logical or 0/1 values.', name);
end

function bit_position = tr_switch_bit(channel)
if channel <= 16
    bit_position = channel + 15;
else
    bit_position = channel - 17;
end
end

function bit_position = pulser_bit(channel)
if channel <= 16
    if mod(channel, 2) == 0
        bit_position = 23 + channel / 2;
    else
        bit_position = 16 + (channel - 1) / 2;
    end
else
    if mod(channel, 2) == 0
        bit_position = 8 + (channel - 18) / 2;
    else
        bit_position = (channel - 17) / 2;
    end
end
end

