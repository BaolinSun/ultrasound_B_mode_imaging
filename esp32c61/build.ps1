param(
    [ValidateSet('build', 'size', 'flash', 'monitor', 'menuconfig')]
    [string]$Action = 'build',
    [string]$Port = '',
    [string]$IdfPath = 'D:\Program Files\esp\v6.1\esp-idf',
    [string]$ActivationScript = 'C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1'
)
$ErrorActionPreference = 'Stop'
$env:PYTHONUTF8 = '1'
if (Test-Path -LiteralPath $ActivationScript) {
    . $ActivationScript
} elseif (-not $env:IDF_PYTHON_ENV_PATH) {
    throw 'Run this script from an activated ESP-IDF 6.1 shell or supply -ActivationScript.'
}
$env:IDF_PATH = $IdfPath
# Keep all compilation output in this project, without an external ccache.
$env:IDF_CCACHE_ENABLE = '0'
$idfPython = Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts\python.exe'
if (-not (Test-Path -LiteralPath $idfPython)) { throw "IDF Python not found: $idfPython" }
$idfTool = Join-Path $IdfPath 'tools\idf.py'
$version = & $idfPython $idfTool --version
if ($LASTEXITCODE -ne 0 -or "$version" -notmatch 'ESP-IDF v6\.1(?:\D|$)') {
    throw "ESP-IDF 6.1 is required. Detected: $version"
}
if ($Action -in @('flash', 'monitor') -and -not $Port) { throw '-Port COMx is required.' }
Push-Location $PSScriptRoot
try {
    if (-not (Test-Path 'sdkconfig')) {
        if (Test-Path 'build') {
            # A failed first configure may leave a directory that fullclean
            # refuses to remove. Reconfigure it explicitly without deletion.
            & $idfPython $idfTool '-DIDF_TARGET=esp32c61' reconfigure
        } else {
            & $idfPython $idfTool set-target esp32c61
        }
        if ($LASTEXITCODE -ne 0) { throw 'target configuration failed' }
    } elseif (-not (Select-String -Path sdkconfig -Pattern '^CONFIG_IDF_TARGET="esp32c61"$' -Quiet)) {
        throw 'Existing sdkconfig has a different target; select esp32c61 explicitly in an IDF shell.'
    }
    $idfArgs = @()
    if ($Port) { $idfArgs += @('-p', $Port) }
    $idfArgs += $Action
    & $idfPython $idfTool @idfArgs
    if ($LASTEXITCODE -ne 0) { throw "idf.py $Action failed" }
} finally { Pop-Location }
