param(
    [ValidateSet('project','hardware','all')]
    [string]$Stage = 'all'
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Tools = if ($env:AMD_DESIGN_TOOLS_2026_1) {
    [System.IO.Path]::GetFullPath($env:AMD_DESIGN_TOOLS_2026_1)
} else {
    'D:\AMDDesignTools\2026.1'
}
$Vivado = Join-Path $Tools 'Vivado\bin\vivado.bat'
$Bootgen = Join-Path $Tools 'Vivado\bin\bootgen.bat'

foreach ($RequiredTool in $Vivado, $Bootgen) {
    if (-not (Test-Path -LiteralPath $RequiredTool -PathType Leaf)) {
        throw "AMD Design Tools 2026.1 executable was not found: $RequiredTool"
    }
}

$env:APPDATA = Join-Path $Root '.vivado-user'
$env:LOCALAPPDATA = $env:APPDATA
$env:HOME = $env:APPDATA
$env:IPYTHONDIR = Join-Path $Root 'build\ipython'
New-Item -ItemType Directory -Force -Path $env:APPDATA,$env:IPYTHONDIR,(Join-Path $Root 'build\vivado-temp') | Out-Null

$VivadoMode = if ($Stage -eq 'project') { 'project' } else { 'build' }
Push-Location (Join-Path $Root 'build')
try {
    # `vivado -init` loads build\vivado.tcl before the requested build script.
    & $Vivado -mode batch -init -tempDir (Join-Path $Root 'build\vivado-temp') `
        -journal (Join-Path $Root 'build\vivado.jou') `
        -log (Join-Path $Root 'build\vivado.log') `
        -source (Join-Path $PSScriptRoot 'create_vivado_2026_1.tcl') `
        -tclargs $VivadoMode
    $VivadoExitCode = $LASTEXITCODE
}
finally {
    Pop-Location
}
if ($VivadoExitCode -ne 0) { throw "Vivado build failed with exit code $VivadoExitCode" }

if ($Stage -eq 'all') {
    & (Join-Path $PSScriptRoot 'run_vitis_2026_1.ps1') -Stage all
    & (Join-Path $PSScriptRoot 'create_boot_image.ps1') -Bootgen $Bootgen
}
