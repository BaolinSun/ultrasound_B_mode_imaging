param(
    [ValidateSet('build', 'size', 'flash', 'monitor', 'menuconfig')]
    [string]$Action = 'build',
    [string]$Port = '',
    [ValidateSet('default', 'throughput')]
    [string]$Profile = 'default',
    [ValidateSet('auto', 'tcp', 'udp')]
    [string]$Transport = 'auto',
    [ValidateSet('auto', 'baseline', 'mid', 'fast', 'faster')]
    [string]$TcpTuning = 'auto',
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
    $configFile = Join-Path $PSScriptRoot 'sdkconfig'
    $buildDir = Join-Path $PSScriptRoot 'build'
    $profileArgs = @()
    $resolvedTuning = if ($TcpTuning -ne 'auto') { $TcpTuning } elseif ($Profile -eq 'throughput') { 'fast' } else { 'baseline' }
    if ($Profile -eq 'throughput' -or $Transport -ne 'auto' -or $TcpTuning -ne 'auto') {
        # Isolate all generated configuration/output; never reset user's sdkconfig.
        $name = if ($Transport -eq 'auto') { "build-$Profile-$resolvedTuning-socket" } else { "build-$Profile-$Transport-$resolvedTuning-socket" }
        $buildDir = Join-Path $PSScriptRoot $name
        $configFile = Join-Path $buildDir 'sdkconfig'
        New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
        $defaults = Join-Path $PSScriptRoot 'sdkconfig.defaults'
        if ($Profile -eq 'throughput') { $defaults += ';' + (Join-Path $PSScriptRoot 'sdkconfig.throughput.defaults') }
        $defaults += ';' + (Join-Path $PSScriptRoot "sdkconfig.tcp-$resolvedTuning.defaults")
        $profileArgs = @('-B', $buildDir, "-DSDKCONFIG=$configFile", "-DSDKCONFIG_DEFAULTS=$defaults")
    }
    # Verification overrides live in separate directories. Normal builds always
    # clear the cache override and select the C header, never a sticky -D value.
    $override = if ($Transport -eq 'tcp') { '1' } elseif ($Transport -eq 'udp') { '0' } else { '' }
    $profileArgs += "-DWIFI_FRAME_TRANSPORT_OVERRIDE=$override"
    if (-not (Test-Path -LiteralPath $configFile)) {
        if (Test-Path -LiteralPath $buildDir) {
            # A failed first configure may leave a directory that fullclean
            # refuses to remove. Reconfigure it explicitly without deletion.
            & $idfPython $idfTool @profileArgs '-DIDF_TARGET=esp32c61' reconfigure
        } else {
            & $idfPython $idfTool @profileArgs set-target esp32c61
        }
        if ($LASTEXITCODE -ne 0) { throw 'target configuration failed' }
    } elseif (-not (Select-String -LiteralPath $configFile -Pattern '^CONFIG_IDF_TARGET="esp32c61"$' -Quiet)) {
        throw 'Existing sdkconfig has a different target; select esp32c61 explicitly in an IDF shell.'
    }
    $idfArgs = @($profileArgs)
    if ($Port) { $idfArgs += @('-p', $Port) }
    $idfArgs += $Action
    & $idfPython $idfTool @idfArgs
    if ($LASTEXITCODE -ne 0) { throw "idf.py $Action failed" }
} finally { Pop-Location }
