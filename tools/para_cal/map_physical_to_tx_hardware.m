function hardware_values = map_physical_to_tx_hardware(physical_values, channel_map)
%MAP_PHYSICAL_TO_TX_HARDWARE Map natural physical-element values to TX channels.
%
% Each row is an independent configuration. Column N on input always means
% physical element N; column N on output always means TX global channel N.

if nargin < 2 || isempty(channel_map)
    channel_map = load_probe_channel_map();
end

assert(size(physical_values, 2) == 64, ...
    'physical_values must have exactly 64 columns in physical-element order.');

hardware_values = zeros(size(physical_values), 'like', physical_values);
hardware_values(:, channel_map.tx_global_channel) = ...
    physical_values(:, channel_map.physical_element);
end

