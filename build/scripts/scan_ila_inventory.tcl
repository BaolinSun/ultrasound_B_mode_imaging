# Observe existing debug cores only. Never program the device or alter PL registers.
open_hw_manager
connect_hw_server -url localhost:3121
open_hw_target
foreach device [get_hw_devices] {
    puts "DEVICE $device"
    current_hw_device $device
    refresh_hw_device -update_hw_probes false $device
    puts "ILAS [get_hw_ilas -quiet -of_objects $device]"
}
close_hw_manager
