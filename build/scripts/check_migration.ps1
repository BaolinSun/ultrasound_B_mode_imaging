$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Old = Join-Path (Split-Path -Parent $Root) 'ultrasound_focus_B_mode_imaging'
$OldApp = Join-Path $Old 'vitis\B_mode_focus_imaging\src'
$NewApp = Join-Path $Root 'build\src\ps'

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

function Assert-TextEqual([string]$OldRelative, [string]$NewRelative) {
    $OldText = Normalize-Source (Join-Path $OldApp $OldRelative)
    $NewText = Normalize-Source (Join-Path $NewApp $NewRelative)
    if ($OldText -cne $NewText) {
        throw "Protected source content changed: $NewRelative"
    }
}

function Assert-CodeEqual([string]$OldRelative, [string]$NewRelative) {
    $OldText = Normalize-Source (Join-Path $OldApp $OldRelative)
    $NewText = Normalize-Source (Join-Path $NewApp $NewRelative)
    $OldCode = (($OldText -replace '(?m)//.*$', '') -replace '\s+', '')
    $NewCode = (($NewText -replace '(?m)//.*$', '') -replace '\s+', '')
    if ($OldCode -cne $NewCode) {
        throw "Protected source code changed: $NewRelative"
    }
}

# Files containing TX configuration, UART protocol definitions, B-mode tables,
# and imaging constants were only transcoded to UTF-8; their decoded source
# text must remain identical.
foreach ($Relative in @(
    'TX7332\tx7332.c',
    'TX7332\tx7332.h',
    'TX7332\tx7332_spi.c',
    'TX7332\tx7332_spi.h',
    'uart\uart.h',
    'bram\demod_coeffs.c',
    'bram\log_table.c',
    'usb\xusbps_ch9_winusb.c',
    'usb\xusbps_Ringbuffer.c',
    'usb\xusbps_Ringbuffer.h'
)) {
    Assert-TextEqual $Relative $Relative
}
Assert-CodeEqual 'bram\bram_config.c' 'bram\bram_config.c'
Assert-TextEqual 'bram\delay_profile.c' 'bram\delay_profile.c'

# The AFE source has migration-only base-address/interrupt edits. Compare every
# register-write expression instead of requiring the complete file to be equal.
$OldAfe = Read-SourceText (Join-Path $OldApp 'AFE5832\afe5832.c')
$NewAfe = Read-SourceText (Join-Path $NewApp 'AFE5832\afe5832.c')
$Pattern = 'afe5832_spi_write_byte\s*\([^;]*\);'
$OldWrites = [regex]::Matches($OldAfe, $Pattern, 'Singleline') | ForEach-Object { $_.Value -replace '\s+', '' }
$NewWrites = [regex]::Matches($NewAfe, $Pattern, 'Singleline') | ForEach-Object { $_.Value -replace '\s+', '' }
if (Compare-Object $OldWrites $NewWrites -SyncWindow 0) {
    throw 'AFE5832 register-write sequence changed during migration.'
}
Assert-TextEqual 'AFE5832\afe5832_spi.c' 'AFE5832\afe5832_spi.c'

$OldImports = Join-Path $Old 'ultrasound_focus_B_mode_imaging.srcs\sources_1\imports'
$NewImports = Join-Path $Root 'build\src\imports'
foreach ($OldRtl in Get-ChildItem $OldImports -Recurse -File |
        Where-Object { $_.Extension -in '.v', '.sv', '.vh' }) {
    $Relative = $OldRtl.FullName.Substring($OldImports.Length + 1)
    $NewRtl = Join-Path $NewImports $Relative
    if ($Relative -in @(
        'vivado\Wearable_US\Wearable_US.srcs\sources_1\imports\src\afe5832_adc_14b_16lane.v',
        'sources\proc_top.sv'
    )) {
        continue
    }
    if (-not (Test-Path $NewRtl -PathType Leaf) -or
        (Get-FileHash $OldRtl.FullName -Algorithm SHA256).Hash -ne
        (Get-FileHash $NewRtl -Algorithm SHA256).Hash) {
        throw "Non-migration RTL changed during migration: $Relative"
    }
}
$AdcRelative = 'vivado\Wearable_US\Wearable_US.srcs\sources_1\imports\src\afe5832_adc_14b_16lane.v'
$OldAdc = Normalize-Source (Join-Path $OldImports $AdcRelative)
$NewAdc = Normalize-Source (Join-Path $NewImports $AdcRelative)
$AsyncAttribute = '\(\*\s*ASYNC_REG\s*=\s*"TRUE"\s*\*\)\s*'
if (($OldAdc -replace $AsyncAttribute, '') -cne ($NewAdc -replace $AsyncAttribute, '')) {
    throw 'ADC RTL contains changes beyond ASYNC_REG migration attributes.'
}

$ProcTopRelative = 'sources\proc_top.sv'
$OldProcTop = Normalize-Source (Join-Path $OldImports $ProcTopRelative)
$NewProcTop = Normalize-Source (Join-Path $NewImports $ProcTopRelative)
$SyncAttribute = '\(\*\s*ASYNC_REG\s*=\s*"TRUE"(?:\s*,\s*SHREG_EXTRACT\s*=\s*"NO"\s*,\s*DONT_TOUCH\s*=\s*"TRUE")?\s*\*\)\s*'
$ExplicitScanEnable = '(?m)^\s*wire\s+scan_enable\s*;\s*\r?\n?'
$OldProcTopMigrationNormalized = ((($OldProcTop -replace $SyncAttribute, '') -replace $ExplicitScanEnable, '') -replace '\s+', '')
$NewProcTopMigrationNormalized = ((($NewProcTop -replace $SyncAttribute, '') -replace $ExplicitScanEnable, '') -replace '\s+', '')
if ($OldProcTopMigrationNormalized -cne $NewProcTopMigrationNormalized) {
    throw 'proc_top RTL contains changes beyond CDC preservation attributes and explicit net declaration.'
}

$Bd = Get-Content (Join-Path $Root 'build\src\bd\pus_ctrl_system.bd') -Raw
if ($Bd -match 'axi_dma') { throw 'AXI DMA unexpectedly appears in the block design.' }
if ($Bd -notmatch '0x43C00000' -or $Bd -notmatch '0x40000000') {
    throw 'Expected AXI base address is missing from the block design.'
}

$XParameters = Join-Path $Root 'workspace\ubmi_p\export\ubmi_p\sw\standalone_ps7_cortexa9_0\include\xparameters.h'
$XParametersText = Get-Content $XParameters -Raw
foreach ($Expected in @(
    'XPAR_XAFE5832_CTRL_PS_0_BASEADDR 0x43c00000',
    'XPAR_XBRAM_0_BASEADDR 0x40000000',
    'XPAR_PS7_DDR_0_BASEADDRESS 0x100000'
)) {
    if ($XParametersText -notmatch [regex]::Escape($Expected)) {
        throw "Generated xparameters.h does not contain: $Expected"
    }
}
if ($XParametersText -match 'AXIDMA|XPAR_FABRIC_') {
    throw 'Generated xparameters.h unexpectedly contains AXI DMA or PL interrupt macros.'
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

$Bootgen = 'C:\AMDDesignTools\2026.1\Vivado\bin\bootgen.bat'
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

$OldManifest = Join-Path $Root 'build\reports\old_source_manifest.sha256'
foreach ($Line in Get-Content $OldManifest) {
    if ($Line -notmatch '^([0-9a-fA-F]{64})  (.+)$') {
        continue
    }
    $ExpectedHash = $Matches[1]
    $OldPath = Join-Path $Old $Matches[2]
    if (-not (Test-Path $OldPath -PathType Leaf) -or
        (Get-FileHash $OldPath -Algorithm SHA256).Hash.ToLowerInvariant() -cne
        $ExpectedHash.ToLowerInvariant()) {
        throw "Old read-only source changed after migration started: $($Matches[2])"
    }
}

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
