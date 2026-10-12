param([string]$Workspace = '')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (-not $Workspace) { $Workspace=Join-Path $projectRoot 'workspace' }
$env:ULTRASOUND_REPAIR_WORKSPACE=[IO.Path]::GetFullPath($Workspace)
$tools='D:/AMDDesignTools/2026.1'
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$env:LANG='en_US.UTF-8'
$env:LC_ALL='en_US.UTF-8'
$env:PYTHONIOENCODING='utf-8'
$env:PYTHONUTF8='1'
New-Item -ItemType Directory -Force -Path 'D:/vt' | Out-Null
$env:TEMP='D:/vt'
$env:TMP='D:/vt'
$env:IPYTHONDIR=Join-Path $projectRoot 'build/ipython'
& "$tools/Vitis/bin/vitis.bat" -s (Join-Path $PSScriptRoot 'repair_vitis_workspace.py') 2>&1 |
    Tee-Object -Variable repairOutput
if ($LASTEXITCODE -ne 0 -or -not ($repairOutput -match '^VITIS_WORKSPACE_REPAIR_COMPLETE:')) {
    throw 'Existing Vitis workspace repair/build failed; no successful ELF is being reported.'
}
