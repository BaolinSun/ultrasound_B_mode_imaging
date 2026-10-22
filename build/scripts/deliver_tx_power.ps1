param([Parameter(Mandatory=$true)][string]$PlatformWorkspace,[string]$Tag='20261022')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$out=Join-Path $root "build/output/tx_power_$Tag"
$review=Join-Path $root 'build/tx_power_review.log'
if(!(Test-Path -LiteralPath $review) -or !(Select-String -LiteralPath $review -Pattern '^TX_IMPLEMENTED_VERIFIED:' -Quiet)) {
    throw 'Final routed TX review has not passed; refusing stale artifacts.'
}
$reviewText=Get-Content -LiteralPath $review -Raw
foreach($ext in @('bit','xsa')) {
    $pattern="(?m)^TX_REVIEW_${ext}_SHA256=([A-Fa-f0-9]{64})\s*$"
    $match=[regex]::Match($reviewText,$pattern)
    if(!$match.Success -or (Get-FileHash -LiteralPath (Join-Path $root "build/output/ultrasound_B_mode_imaging.$ext")).Hash -ne $match.Groups[1].Value){throw "Reviewed $ext does not match delivery"}
}
$boot=Join-Path $root 'build/output/BOOT.BIN'
$savedBoot=Join-Path $root 'build/backups/tx_power_092cad8/BOOT.BIN'
if((Get-FileHash -LiteralPath $boot).Hash -ne (Get-FileHash -LiteralPath $savedBoot).Hash){throw 'BOOT.BIN unexpectedly changed'}
$platformXsa=Join-Path $PlatformWorkspace 'ubmi_p/hw/ultrasound_B_mode_imaging.xsa'
if((Get-FileHash -LiteralPath $platformXsa).Hash -ne (Get-FileHash -LiteralPath (Join-Path $root 'build/output/ultrasound_B_mode_imaging.xsa')).Hash){throw 'Platform was not generated from the reviewed XSA'}
$bsp=[IO.Path]::GetFullPath((Join-Path $PlatformWorkspace 'ubmi_p/export/ubmi_p/sw/standalone_ps7_cortexa9_0')).Replace('\','/')
foreach($mode in @('sdio','usb')) {
    $cache=Get-Content -LiteralPath (Join-Path $root "build/vitis_frame_store_$mode/CMakeCache.txt") -Raw
    if(!$cache.Contains("CMAKE_LIBRARY_PATH:UNINITIALIZED=$bsp/lib")){throw "$mode ELF does not use the delivery BSP"}
}
New-Item -ItemType Directory -Force -Path $out | Out-Null
$artifacts=[Collections.Generic.List[object]]::new()
function Publish-TxFile([string]$Source,[string]$Name) {
    if(!(Test-Path -LiteralPath $Source -PathType Leaf)){throw "Missing $Source"}
    $dest=Join-Path $out $Name
    Copy-Item -LiteralPath $Source -Destination $dest
    $artifacts.Add(@{path=[IO.Path]::GetFullPath($dest);sha256=(Get-FileHash -LiteralPath $dest).Hash;bytes=(Get-Item -LiteralPath $dest).Length})
}
foreach($ext in @('bit','xsa','ltx')){Publish-TxFile (Join-Path $root "build/output/ultrasound_B_mode_imaging.$ext") "ultrasound_B_mode_imaging.$ext"}
foreach($mode in @('sdio','usb')){Publish-TxFile (Join-Path $root "build/vitis_frame_store_$mode/B_mode_focus_imaging.elf") "B_mode_focus_imaging_$mode.elf"}
$fsbl=Get-ChildItem -LiteralPath (Join-Path $PlatformWorkspace 'ubmi_p') -Recurse -File -Filter 'fsbl.elf' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if(!$fsbl){throw 'Fresh platform FSBL missing'}
Publish-TxFile $fsbl.FullName 'zynq_fsbl.elf'
$normal=Join-Path $root 'esp32c61/build-throughput-fast-socket-exp-combined'
foreach($pair in @(@('ultrasound_psram_udp.bin','esp32c61_combined_tcp.bin'),@('bootloader/bootloader.bin','esp32c61_bootloader.bin'),@('partition_table/partition-table.bin','esp32c61_partition_table.bin'))){Publish-TxFile (Join-Path $normal $pair[0]) $pair[1]}
foreach($pair in @(@('build-throughput-tcp-fast-socket-exp-combined','esp32c61_throughput_tcp.bin'),@('build-throughput-udp-fast-socket-exp-combined','esp32c61_throughput_udp.bin'),@('build-default-tcp-baseline-socket','esp32c61_default_tcp.bin'),@('build-default-udp-baseline-socket','esp32c61_default_udp.bin'))){Publish-TxFile (Join-Path $root "esp32c61/$($pair[0])/ultrasound_psram_udp.bin") $pair[1]}
# Standard GUI entrypoints: copies only, never bootgen/JTAG/flash.
Copy-Item -LiteralPath (Join-Path $out 'B_mode_focus_imaging_sdio.elf') -Destination (Join-Path $root 'build/output/B_mode_focus_imaging.elf')
Copy-Item -LiteralPath (Join-Path $out 'zynq_fsbl.elf') -Destination (Join-Path $root 'build/output/zynq_fsbl.elf')
$ide=Join-Path $root 'workspace/B_mode_focus_imaging/_ide/bitstream'
New-Item -ItemType Directory -Force -Path $ide | Out-Null
Copy-Item -LiteralPath (Join-Path $out 'ultrasound_B_mode_imaging.bit') -Destination (Join-Path $ide 'ultrasound_B_mode_imaging.bit')
foreach($relative in @('workspace/ubmi_p/hw/ultrasound_B_mode_imaging.xsa','workspace/ubmi_p/export/ubmi_p/hw/ultrasound_B_mode_imaging.xsa')) {
    $dest=Join-Path $root $relative
    if(Test-Path -LiteralPath (Split-Path -Parent $dest)){Copy-Item -LiteralPath (Join-Path $out 'ultrasound_B_mode_imaging.xsa') -Destination $dest}
}
$ideElf=Join-Path $root 'workspace/B_mode_focus_imaging/build/B_mode_focus_imaging.elf'
if(Test-Path -LiteralPath (Split-Path -Parent $ideElf)){Copy-Item -LiteralPath (Join-Path $out 'B_mode_focus_imaging_sdio.elf') -Destination $ideElf}
$manifest=@{base_commit='092cad8';generated_at=(Get-Date -Format o);branch='feature/tx7332-power-control';artifacts=$artifacts;platform_workspace=[IO.Path]::GetFullPath($PlatformWorkspace);board_test='NOT_RUN';boot_bin=@{path=$boot;sha256=(Get-FileHash -LiteralPath $boot).Hash;unchanged=$true}}
$manifest | ConvertTo-Json -Depth 6 | Out-File -LiteralPath (Join-Path $root 'esp32c61/logs/tx_power_build_manifest.json') -Encoding utf8
$artifacts | ForEach-Object {Write-Output ("TX_ARTIFACT {0} SHA256={1}" -f $_.path,$_.sha256)}
Write-Output 'TX_DELIVERY_COMPLETE: no BOOT.BIN/flash/JTAG operations'
