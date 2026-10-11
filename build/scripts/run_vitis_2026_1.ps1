param(
    [ValidateSet('platform','all')]
    [string]$Stage = 'all'
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
& chcp.com 65001 | Out-Null
$env:LANG = 'en_US.UTF-8'
$env:LC_ALL = 'en_US.UTF-8'
$env:PYTHONIOENCODING = 'utf-8'
$env:PYTHONUTF8 = '1'
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Tools = if ($env:AMD_DESIGN_TOOLS_2026_1) {
    [System.IO.Path]::GetFullPath($env:AMD_DESIGN_TOOLS_2026_1)
} else {
    'D:\AMDDesignTools\2026.1'
}
$Vitis = Join-Path $Tools 'Vitis\bin\vitis.bat'
if (-not (Test-Path -LiteralPath $Vitis)) {
    throw "Vitis 2026.1 was not found at $Vitis"
}

# JDK 24 uses a Windows AF_UNIX socket for the NIO selector wakeup pipe.
# Keep TEMP/TMP short so the Vitis gRPC server can create that socket when
# this script is launched from a managed desktop command environment.
$VitisTemp = 'D:\vt'
New-Item -ItemType Directory -Force -Path $VitisTemp | Out-Null
$env:TEMP = $VitisTemp
$env:TMP = $VitisTemp

$env:IPYTHONDIR = Join-Path $Root 'build\ipython'
$env:ULTRASOUND_VITIS_STAGE = $Stage
New-Item -ItemType Directory -Force -Path $env:IPYTHONDIR | Out-Null

& $Vitis -s (Join-Path $PSScriptRoot 'build_vitis_2026_1.py') 2>&1 | Tee-Object -Variable vitisOutput
if ($LASTEXITCODE -ne 0) { throw "Vitis exited with code $LASTEXITCODE" }
# The Vitis launcher can exit 0 after a Python exception. Existing output ELFs
# are not evidence of this run succeeding; require explicit completion markers.
$completed=@($vitisOutput | Where-Object { "$_" -match '^VITIS_MIGRATION_COMPLETE:' })
$expectedMarkers=if ($Stage -eq 'all') { 2 } else { 1 }
if ($completed.Count -ne $expectedMarkers) { throw 'Vitis did not complete this build; refusing stale artifacts.' }

$WorkspaceRoot = if ($env:ULTRASOUND_VITIS_WORKSPACE) {
    $env:ULTRASOUND_VITIS_WORKSPACE
} else {
    Join-Path $Root 'workspace'
}
$PlatformRoot = Join-Path $WorkspaceRoot 'ubmi_p'
if (-not (Get-ChildItem $PlatformRoot -Recurse -Filter '*.xpfm' -File -ErrorAction SilentlyContinue)) {
    throw 'Vitis platform build did not produce an XPFM.'
}
if ($Stage -eq 'all') {
    foreach ($Required in 'B_mode_focus_imaging.elf','zynq_fsbl.elf') {
        if (-not (Test-Path (Join-Path $Root "build\output\$Required"))) {
            throw "Vitis did not produce $Required"
        }
    }
}
