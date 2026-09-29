$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Tools = if ($env:AMD_DESIGN_TOOLS_2026_1) {
    [System.IO.Path]::GetFullPath($env:AMD_DESIGN_TOOLS_2026_1)
} else {
    'D:\AMDDesignTools\2026.1'
}
$Old = Join-Path (Split-Path -Parent $Root) 'ultrasound_focus_B_mode_imaging'
$NewApp = Join-Path $Root 'build\src\ps'
$WorkspaceRoot = if ($env:ULTRASOUND_VITIS_WORKSPACE) {
    [System.IO.Path]::GetFullPath($env:ULTRASOUND_VITIS_WORKSPACE)
} else {
    Join-Path $Root 'workspace'
}
$SourceCommit = 'f225a21f095718dbce4ada470ad85f1f9bbc4a14'
$SafeDirectory = "safe.directory=$($Old.Replace('\', '/'))"

function Read-SourceText([string]$Path) {
    $Bytes = [System.IO.File]::ReadAllBytes($Path)
    $StrictUtf8 = [System.Text.UTF8Encoding]::new($false, $true)
    try {
        return $StrictUtf8.GetString($Bytes)
    }
    catch {
        return [System.Text.Encoding]::GetEncoding(936).GetString($Bytes)
    }
}

function Normalize-Source([string]$Path) {
    return (Read-SourceText $Path).Replace("`r`n", "`n").TrimEnd()
}

# Validate the immutable Git object set used for this migration.  This avoids
# depending on the source repository's current branch or dirty working tree.
$ResolvedCommit = (& git -c $SafeDirectory -C $Old rev-parse "$SourceCommit^{commit}" 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $ResolvedCommit -cne $SourceCommit) {
    throw "Fixed source commit is unavailable: $SourceCommit"
}
$ExpectedParent = '6f7e0d7b36b4c3f26afa1be6e0278ace3ca06581'
$ResolvedParent = (& git -c $SafeDirectory -C $Old rev-parse "$SourceCommit^" 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $ResolvedParent -cne $ExpectedParent) {
    throw "Incremental source parent is not the previously migrated commit: $ExpectedParent"
}
$ExpectedIncrementalPaths = @(
    'vitis/B_mode_focus_imaging/src/HV/hv.c',
    'vitis/B_mode_focus_imaging/src/bram/bram.c',
    'vitis/B_mode_focus_imaging/src/bram/bram.h',
    'vitis/B_mode_focus_imaging/src/main.c',
    'vitis/B_mode_focus_imaging/src/main.h',
    'vitis/B_mode_focus_imaging/src/uart/uart.c'
)
$ActualIncrementalPaths = @(& git -c $SafeDirectory -C $Old diff --name-only $ExpectedParent $SourceCommit)
if ($LASTEXITCODE -ne 0 -or (Compare-Object $ExpectedIncrementalPaths $ActualIncrementalPaths)) {
    throw 'The fixed incremental source range no longer contains exactly the six reviewed PS files.'
}
$BlobManifest = Join-Path $Root 'build\reports\source_commit_manifest.gitblob'
foreach ($Line in Get-Content $BlobManifest) {
    if ($Line -notmatch '^([0-9a-f]{40})  (.+)$') { continue }
    $ExpectedBlob = $Matches[1]
    $GitPath = $Matches[2]
    $ActualBlob = (& git -c $SafeDirectory -C $Old rev-parse "$SourceCommit`:$GitPath" 2>&1 | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $ActualBlob -cne $ExpectedBlob) {
        throw "Source Git blob mismatch at $SourceCommit`: $GitPath"
    }
}

# Functional and CDC invariants intentionally retained/adapted in the 2026.1
# target. Detailed arithmetic and shot sequencing are covered by XSim tests.
$RtlRoot = Join-Path $Root 'build\src\imports\sources'
$RtlText = @{}
foreach ($Entry in @{
    Define = 'fpga_define.vh'; ProcTop = 'proc_top.sv'; Filter = 'process_top\filter_iq.sv'
    Scan = 'scan_sync.v'; TxTop = 'tx_top\tx_top.v'; ParaReg = 'para_top\para_reg.sv'
}.GetEnumerator()) {
    $RtlText[$Entry.Key] = Read-SourceText (Join-Path $RtlRoot $Entry.Value)
}
foreach ($Invariant in @(
    @('Define', 'ADDR_RX_GAIN_SHIFT\s+20''h11530'),
    @('ProcTop', 'beamform_active_sync_ff1'),
    @('Filter', 'gain_shift_sync_ff1'),
    @('Scan', 'rtc_shot'),
    @('TxTop', 'tx_spi_rdreq_sync'),
    @('TxTop', 'tx_spi_rdreq_pending'),
    @('ParaReg', 'tx_spi_rxvalid_sync'),
    @('ParaReg', 'tx_spi_req_busy')
)) {
    if ($RtlText[$Invariant[0]] -notmatch $Invariant[1]) {
        throw "Missing migrated RTL invariant: $($Invariant[0]) / $($Invariant[1])"
    }
}

$ImagingConfig = Read-SourceText (Join-Path $NewApp 'imaging_config.h')
if ($ImagingConfig -notmatch 'PI_SHOTS_PER_LINE\s+4' -or
    $ImagingConfig -notmatch 'PI_SHOTS_PER_FRAME\s+(?:256|\(BF_LINE_NUM\s*\*\s*PI_SHOTS_PER_LINE\))') {
    throw 'PI-THI shot constants are missing from imaging_config.h.'
}
$BramConfig = Read-SourceText (Join-Path $NewApp 'bram\bram_config.c')
if ($BramConfig -notmatch '0x0007B9BA') {
    throw 'TX7332 startup register 0x120 value is not 0x0007B9BA.'
}
$UartSource = Read-SourceText (Join-Path $NewApp 'uart\uart.c')
foreach ($CommandPattern in @(
    'strcmp\(cmd,\s*"GAIN"\)',
    'strcmp\(cmd,\s*"TX7332"\)',
    'strcmp\(op,\s*"SET"\)',
    'strcmp\(op,\s*"RD"\)',
    'op\s*==\s*NULL\s*\|\|\s*strcmp\(op,\s*"WR"\)'
)) {
    if ($UartSource -notmatch $CommandPattern) {
        throw "UART command route is missing: $CommandPattern"
    }
}
$BramSource = Read-SourceText (Join-Path $NewApp 'bram\bram.c')
$BramHeader = Read-SourceText (Join-Path $NewApp 'bram\bram.h')
if ($UartSource -notmatch 'BEAM_ACTIVE_USE_BEAM_STOP' -or
    $BramSource -notmatch 'BRAM_CFG_STATUS_BUSY' -or
    $BramHeader -notmatch 'BRAM_CFG_STATUS_BUSY') {
    throw 'TX7332 readback does not enforce BEAM STOP or expose busy status.'
}

# Imaging lifecycle added by f225a21. Keep full configuration transactions
# separate from the BEAM pause/resume commands, which intentionally do not
# change HV state.
$HvSource = Read-SourceText (Join-Path $NewApp 'HV\hv.c')
$HvHeader = Read-SourceText (Join-Path $NewApp 'HV\hv.h')
$MainSource = Read-SourceText (Join-Path $NewApp 'main.c')
$MainHeader = Read-SourceText (Join-Path $NewApp 'main.h')
foreach ($LifecycleInvariant in @(
    @($BramSource, 'void\s+Beamform_Resume\s*\([^)]*\).*?Beamform_SetControl\(0U,\s*0U\).*?usleep\(100\).*?Beamform_SetControl\(0U,\s*1U\).*?usleep\(5000\).*?Beamform_SetControl\(1U,\s*1U\)'),
    @($BramSource, 'void\s+Beamform_Pause\s*\([^)]*\).*?Beamform_SetControl\(0U,\s*1U\)'),
    @($BramSource, 'void\s+Beamform_Halt\s*\([^)]*\).*?Beamform_SetControl\(0U,\s*0U\)'),
    @($BramSource, 'void\s+Beamform_Stop\s*\([^)]*\).*?hv_disable\(\).*?Beamform_Halt\(\).*?sleep\(1\)'),
    @($BramSource, 'void\s+para_down\s*\([^)]*\).*?sin_beta_para\(\).*?hv_disable\(\).*?Beamform_Halt\(\)'),
    @($UartSource, 'strcmp\(op,\s*"END"\).*?Beamform_Start\(\).*?"CFG_END"'),
    @($UartSource, 'strcmp\(op,\s*"ABORT"\).*?Beamform_Stop\(\).*?"CFG_ABORT"'),
    @($UartSource, 'static\s+void\s+uart_handle_beam.*?strcmp\(op,\s*"START"\).*?Beamform_Resume\(\).*?strcmp\(op,\s*"STOP"\).*?Beamform_Pause\(\)'),
    @($UartSource, 'static\s+void\s+uart_handle_hv.*?strcmp\(op,\s*"ENABLE"\).*?hv_enable\(\).*?hv_is_enabled\(\).*?strcmp\(op,\s*"DISABLE"\).*?hv_disable\(\).*?Beamform_Halt\(\)'),
    @($UartSource, 'strcmp\(cmd,\s*"HV"\).*?uart_handle_hv'),
    @($HvSource, 'void\s+hv_enable.*?HV_EN_PIN,\s*1\).*?LED_1_EMIO_PIN,\s*1\)'),
    @($HvSource, 'void\s+hv_disable.*?HV_EN_PIN,\s*0\).*?LED_1_EMIO_PIN,\s*0\)'),
    @($HvHeader, 'u32\s+hv_is_enabled\s*\(void\)'),
    @($MainSource, 'Beamform ready; STOP\.'),
    @($MainHeader, 'LED_1_EMIO_PIN\s+55.*?LED_2_EMIO_PIN\s+54')
)) {
    $LifecyclePattern = '(?s)' + $LifecycleInvariant[1]
    if ($LifecycleInvariant[0] -notmatch $LifecyclePattern) {
        throw "Missing imaging lifecycle invariant: $($LifecycleInvariant[1])"
    }
}

$Bd = Get-Content (Join-Path $Root 'build\src\bd\pus_ctrl_system.bd') -Raw
if ($Bd -match 'axi_dma') { throw 'AXI DMA unexpectedly appears in the block design.' }
if ($Bd -notmatch '0x43C00000' -or $Bd -notmatch '0x40000000') {
    throw 'Expected AXI base address is missing from the block design.'
}

$XsaPath = Join-Path $Root 'build\output\ultrasound_B_mode_imaging.xsa'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$XsaArchive = [System.IO.Compression.ZipFile]::OpenRead($XsaPath)
try {
    $HwhEntry = $XsaArchive.Entries | Where-Object { $_.FullName -eq 'pus_ctrl_system.hwh' }
    if ($null -eq $HwhEntry) {
        throw 'XSA does not contain pus_ctrl_system.hwh.'
    }
    $HwhReader = [System.IO.StreamReader]::new($HwhEntry.Open())
    try {
        $HardwareDefinitionText = $HwhReader.ReadToEnd()
    }
    finally {
        $HwhReader.Dispose()
    }
}
finally {
    $XsaArchive.Dispose()
}
foreach ($Expected in @(
    'NAME="C_S00_AXI_BASEADDR" VALUE="0x43C00000"',
    'NAME="C_S_AXI_BASEADDR" VALUE="0x40000000"',
    'NAME="PCW_DDR_RAM_BASEADDR" VALUE="0x00100000"'
)) {
    if ($HardwareDefinitionText -notmatch [regex]::Escape($Expected)) {
        throw "XSA hardware definition does not contain: $Expected"
    }
}
if ($HardwareDefinitionText -match 'axi_dma') {
    throw 'XSA hardware definition unexpectedly contains AXI DMA.'
}

$IpConfigBefore = Get-Content (Join-Path $Root 'build\reports\ip_configuration_before_upgrade.txt') |
    Where-Object { $_ -match "`tCONFIG\." }
$IpConfigAfter = Get-Content (Join-Path $Root 'build\reports\ip_configuration_after_upgrade.txt') |
    Where-Object { $_ -match "`tCONFIG\." }
if ($IpConfigBefore.Count -eq 0 -or
    $IpConfigBefore.Count -ne $IpConfigAfter.Count -or
    (Compare-Object $IpConfigBefore $IpConfigAfter)) {
    throw 'Official IP CONFIG properties changed during the 2026.1 upgrade.'
}

$TimingText = Get-Content (Join-Path $Root 'build\output\timing_summary.rpt') -Raw
$TimingMatch = [regex]::Match(
    $TimingText,
    'WNS\(ns\).*?\r?\n\s*-+.*?\r?\n\s*(?<WNS>-?\d+\.\d+)\s+(?<TNS>-?\d+\.\d+)\s+\d+\s+\d+\s+(?<WHS>-?\d+\.\d+)\s+(?<THS>-?\d+\.\d+)',
    [System.Text.RegularExpressions.RegexOptions]::Singleline
)
if (-not $TimingMatch.Success -or
    [double]$TimingMatch.Groups['WNS'].Value -lt 0.5 -or
    [double]$TimingMatch.Groups['TNS'].Value -ne 0.0 -or
    [double]$TimingMatch.Groups['WHS'].Value -le 0.0 -or
    [double]$TimingMatch.Groups['THS'].Value -ne 0.0) {
    throw 'Implemented design does not meet WNS >= 0.5 ns, TNS/THS = 0 and WHS > 0.'
}

$UtilizationText = Get-Content (Join-Path $Root 'build\output\utilization.rpt') -Raw
foreach ($Resource in @('Slice LUTs', 'Slice Registers', 'Block RAM Tile', 'DSPs')) {
    $ResourceMatch = [regex]::Match(
        $UtilizationText,
        "(?m)^\|\s*$([regex]::Escape($Resource))\s*\|.*?\|\s*(?<Percent>\d+(?:\.\d+)?)\s*\|\s*$"
    )
    if (-not $ResourceMatch.Success -or
        [double]$ResourceMatch.Groups['Percent'].Value -ge 80.0) {
        throw "$Resource utilization is missing or is not below 80 percent."
    }
}

$XsaPath = Join-Path $Root 'build\output\ultrasound_B_mode_imaging.xsa'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$XsaArchive = [System.IO.Compression.ZipFile]::OpenRead($XsaPath)
try {
    $XsaEntries = @($XsaArchive.Entries | ForEach-Object { $_.FullName })
    foreach ($RequiredXsaEntry in @(
        'ultrasound_B_mode_imaging.bit',
        'drivers/afe5832_ctrl_ps_v1_0/data/afe5832_ctrl_ps.yaml',
        'drivers/afe5832_ctrl_ps_v1_0/src/CMakeLists.txt'
    )) {
        if ($XsaEntries -cnotcontains $RequiredXsaEntry) {
            throw "XSA is missing required entry: $RequiredXsaEntry"
        }
    }
}
finally {
    $XsaArchive.Dispose()
}

$DataRecv = Get-Content (Join-Path $NewApp 'data_transfer\data_recv.c') -Raw
if ($DataRecv -match 'XScuGic|43C10000|DATA_RECV_TX_INTR') {
    throw 'The migrated data receive shim still depends on a PL interrupt or unmapped control register.'
}
$AfeHeader = Get-Content (Join-Path $NewApp 'AFE5832\afe5832.h') -Raw
if ($AfeHeader -match 'ADC_CALI_FINISH_INTR_ID|\b62U\b') {
    throw 'The migrated AFE header still declares a PL calibration interrupt.'
}
$AfeSource = Get-Content (Join-Path $NewApp 'AFE5832\afe5832.c') -Raw
if ($AfeSource -match 'XScuGic_Connect|XScuGic_Enable|ADC_CALI_FINISH_INTR_ID') {
    throw 'The migrated AFE source still registers the absent PL calibration interrupt.'
}
foreach ($InterruptSource in @(
    'key\key.c',
    'uart\uart.c',
    'timer\timer.c',
    'usb\usb.c'
)) {
    $InterruptText = Get-Content (Join-Path $NewApp $InterruptSource) -Raw
    if ($InterruptText -notmatch 'XGet_IntrId' -or
        $InterruptText -notmatch 'XGet_IntrOffset') {
        throw "SDT SPI interrupt ID is not decoded with its GIC offset: $InterruptSource"
    }
}

foreach ($Artifact in @(
    'heart_top.bit',
    'ultrasound_B_mode_imaging.xsa',
    'B_mode_focus_imaging.elf',
    'zynq_fsbl.elf',
    'BOOT.BIN'
)) {
    $ArtifactPath = Join-Path $Root "build\output\$Artifact"
    if (-not (Test-Path $ArtifactPath -PathType Leaf) -or (Get-Item $ArtifactPath).Length -eq 0) {
        throw "Required artifact is missing or empty: $Artifact"
    }
}

$Bootgen = Join-Path $Tools 'Vivado\bin\bootgen.bat'
if (-not (Test-Path -LiteralPath $Bootgen -PathType Leaf)) {
    throw "Bootgen 2026.1 was not found: $Bootgen"
}
$BootImage = Join-Path $Root 'build\output\BOOT.BIN'
$BootRead = (& $Bootgen -arch zynq -read $BootImage 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0) {
    throw "Bootgen could not read the generated BOOT.BIN (exit $LASTEXITCODE)."
}
$BootOrder = @(
    'IMAGE HEADER (zynq_fsbl.elf)',
    'IMAGE HEADER (heart_top.bit)',
    'IMAGE HEADER (B_mode_focus_imaging.elf)'
)
$PreviousIndex = -1
foreach ($Header in $BootOrder) {
    $CurrentIndex = $BootRead.IndexOf($Header, [System.StringComparison]::Ordinal)
    if ($CurrentIndex -le $PreviousIndex) {
        throw "BOOT.BIN partition is missing or out of order: $Header"
    }
    $PreviousIndex = $CurrentIndex
}
[System.IO.File]::WriteAllText(
    (Join-Path $Root 'build\reports\bootgen_read.txt'),
    $BootRead,
    [System.Text.UTF8Encoding]::new($false)
)

$ArtifactManifest = foreach ($Artifact in @(
    'heart_top.bit',
    'ultrasound_B_mode_imaging.xsa',
    'B_mode_focus_imaging.elf',
    'zynq_fsbl.elf',
    'BOOT.BIN'
)) {
    $ArtifactPath = Join-Path $Root "build\output\$Artifact"
    $Hash = (Get-FileHash $ArtifactPath -Algorithm SHA256).Hash.ToLowerInvariant()
    "$Hash  $Artifact"
}
$ArtifactManifest | Set-Content `
    -LiteralPath (Join-Path $Root 'build\reports\artifact_manifest.sha256') `
    -Encoding utf8

Write-Host 'Migration source and build invariants passed.'
