param([string]$Workspace)
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (-not $Workspace) {
    $Workspace=(Get-ChildItem (Join-Path $root 'build') -Directory -Filter 'sdio_vitis_*' |
                Sort-Object Name -Descending | Select-Object -First 1).FullName
}
if (-not $Workspace) { throw 'Supply the new SDIO Vitis workspace with -Workspace.' }
function Assert-EqualFiles($a,$b) {
    $left=[IO.File]::ReadAllText((Join-Path $root $a)).Replace("`r`n","`n").TrimEnd()
    $right=[IO.File]::ReadAllText((Join-Path $root $b)).Replace("`r`n","`n").TrimEnd()
    if ($left -cne $right) { throw "Source copies differ: $a / $b" }
}
Assert-EqualFiles 'build/src/bd/pus_ctrl_system.bd' 'vivado/ultrasound_B_mode_imaging.srcs/sources_1/bd/bd/pus_ctrl_system.bd'
foreach ($file in @('main.c','outbyte.c','uart/uart.c','uart/uart_log.c','uart/uart_log.h','usb/usb.c','data_transfer/sdio_bridge.c',
                     'data_transfer/sdio_bridge.h','data_transfer/output_config.h','data_transfer/sdio_host.c',
                     'data_transfer/sdio_host.h','data_transfer/bridge_protocol.h')) {
    Assert-EqualFiles "build/src/ps/$file" "workspace/B_mode_focus_imaging/src/$file"
    $canonical=[IO.File]::ReadAllText((Join-Path $root "build/src/ps/$file")).Replace("`r`n","`n").TrimEnd()
    $built=[IO.File]::ReadAllText((Join-Path $Workspace "B_mode_focus_imaging/src/$file")).Replace("`r`n","`n").TrimEnd()
    if ($canonical -cne $built) { throw "Built Vitis source differs: $file" }
}
Assert-EqualFiles 'esp32c61/protocol/bridge_protocol.h' 'build/src/ps/data_transfer/bridge_protocol.h'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip=[IO.Compression.ZipFile]::OpenRead((Join-Path $root 'build/output/ultrasound_B_mode_imaging.xsa'))
try {
    $entry=$zip.Entries | Where-Object FullName -eq 'pus_ctrl_system.hwh'
    if (-not $entry) { throw 'XSA has no hardware definition.' }
    $reader=[IO.StreamReader]::new($entry.Open())
    try { $hwh=[xml]$reader.ReadToEnd() } finally { $reader.Dispose() }
    $ps=$hwh.SelectSingleNode('//MODULE[@INSTANCE="processing_system7_0"]')
    foreach ($pair in @(
        @('PCW_SD0_PERIPHERAL_ENABLE','1'),@('PCW_SD1_PERIPHERAL_ENABLE','1'),
        @('PCW_SD0_SD0_IO','MIO 40 .. 45'),@('PCW_SD1_SD1_IO','MIO 10 .. 15'),
        @('PCW_SD1_GRP_CD_ENABLE','0'),@('PCW_SD1_GRP_WP_ENABLE','0'),
        @('PCW_SD1_GRP_POW_ENABLE','0'),@('PCW_SPI1_PERIPHERAL_ENABLE','1'),
        @('PCW_SPI1_GRP_SS1_ENABLE','0'),@('PCW_SPI1_GRP_SS2_ENABLE','0'),
        @('PCW_UART0_PERIPHERAL_ENABLE','1'),@('PCW_UART0_UART0_IO','MIO 26 .. 27'),
        @('PCW_UART1_PERIPHERAL_ENABLE','1'),@('PCW_UART1_UART1_IO','MIO 16 .. 17'))) {
        $actual=$ps.SelectSingleNode("PARAMETERS/PARAMETER[@NAME='$($pair[0])']").VALUE
        if ($actual -ne $pair[1]) { throw "XSA mismatch: $($pair[0])=$actual" }
    }
} finally { $zip.Dispose() }
$parameters=Get-Content (Join-Path $Workspace 'ubmi_p/export/ubmi_p/sw/standalone_ps7_cortexa9_0/include/xparameters.h') -Raw
foreach ($expected in @('XPAR_XSDPS_NUM_INSTANCES 2','XPAR_XSDPS_0_BASEADDR 0xe0100000','XPAR_XSDPS_1_BASEADDR 0xe0101000',
                       'XPAR_XUARTPS_NUM_INSTANCES 2','XPAR_XUARTPS_0_BASEADDR 0xe0000000','XPAR_XUARTPS_1_BASEADDR 0xe0001000',
                       'XPAR_XUARTPS_1_INTERRUPTS 0x4032')) {
    if (-not $parameters.Contains($expected)) { throw "BSP mismatch: $expected" }
}
$bspConfig=Get-Content (Join-Path $Workspace 'ubmi_p/export/ubmi_p/sw/standalone_ps7_cortexa9_0/include/bspconfig.h') -Raw
if ($bspConfig -notmatch '#define STDOUT_BASEADDRESS 0xe0000000' -or
    $bspConfig -notmatch '#define STDIN_BASEADDRESS 0xe0001000') { throw 'BSP console must use UART0 stdout and UART1 stdin.' }
$tools=if ($env:AMD_DESIGN_TOOLS_2026_1) { $env:AMD_DESIGN_TOOLS_2026_1 } else { 'D:/AMDDesignTools/2026.1' }
$nm=Join-Path $tools 'Vitis/gnu/aarch32/nt/gcc-arm-none-eabi/bin/arm-none-eabi-nm.exe'
$config=Get-Content (Join-Path $root 'build/src/ps/data_transfer/output_config.h') -Raw
$selection=[regex]::Match($config,'(?m)^#define DATA_OUTPUT_MODE DATA_OUTPUT_(USB|SDIO)\s*$')
if (-not $selection.Success) { throw 'Output macro must explicitly select DATA_OUTPUT_USB or DATA_OUTPUT_SDIO.' }
$symbol=& $nm -S (Join-Path $root 'build/output/B_mode_focus_imaging.elf') | Select-String '^([0-9a-f]+) ([0-9a-f]+) b test_frame$'
if ($LASTEXITCODE -ne 0) { throw 'ELF symbol read failed.' }
if ($selection.Groups[1].Value -eq 'SDIO') {
    if (-not $symbol) { throw 'SDIO build DDR test frame symbol not found.' }
    $start=[Convert]::ToUInt64($symbol.Matches[0].Groups[1].Value,16)
    $bytes=[Convert]::ToUInt64($symbol.Matches[0].Groups[2].Value,16)
    if ($start%64 -ne 0 -or $bytes -ne 787456 -or $start -lt 0x100000 -or $start+$bytes -gt 0x1c000000) {
        throw 'DDR test frame is misaligned, wrong-sized, or overlaps reserved USB/PL regions.'
    }
    $memorySummary='DDR test frame 0x{0:x}..0x{1:x}' -f $start,($start+$bytes)
} else {
    if ($symbol) { throw 'USB build unexpectedly contains an SDIO DDR test frame.' }
    $memorySummary='USB build: no SDIO DDR test frame'
}
$boot=& (Join-Path $tools 'Vivado/bin/bootgen.bat') -arch zynq -read (Join-Path $root 'build/output/BOOT.BIN') 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) { throw 'BOOT.BIN read failed.' }
$previous=-1
foreach ($name in @('zynq_fsbl.elf','ultrasound_B_mode_imaging.bit','B_mode_focus_imaging.elf')) {
    $index=$boot.IndexOf("IMAGE HEADER ($name)")
    if ($index -le $previous) { throw "BOOT partition missing/out of order: $name" }
    $previous=$index
}
$bit=Join-Path $root 'build/output/ultrasound_B_mode_imaging.bit'
$implemented=Join-Path $root 'vivado/ultrasound_B_mode_imaging.runs/impl_1/heart_top.bit'
if ((Get-FileHash -LiteralPath $bit).Hash -ne (Get-FileHash -LiteralPath $implemented).Hash) {
    throw 'Canonical bitstream differs from current implemented bitstream.'
}
Write-Host "PASS: source copies, XSA SD0/SD1/SPI1/UART0/UART1, BSP UART0 stdout/UART1 stdin, $memorySummary, BOOT order, canonical bit hash"
