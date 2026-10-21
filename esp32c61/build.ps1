param(
    [ValidateSet('activate', 'build', 'size', 'flash', 'monitor', 'menuconfig')]
    [string]$Action = 'build',
    [string]$Port = '',
    [ValidateSet('default', 'throughput')]
    [string]$Profile = 'throughput',
    [ValidateSet('auto', 'tcp', 'udp')]
    [string]$Transport = 'auto',
    [ValidateSet('auto', 'baseline', 'mid', 'fast', 'faster', 'large', 'xlarge')]
    [string]$TcpTuning = 'auto',
    [ValidateSet('none', 'core', 'direct', 'combined', 'psram80')]
    [string]$Experiment = 'combined',
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
# Environment variables and this command remain in the current PowerShell after
# the script returns. Resolve paths at invocation time, not from script locals
# that disappear when invoked normally (without dot-sourcing).
function global:Invoke-idfpy {
    $commandPython = Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts\python.exe'
    $commandTool = Join-Path $env:IDF_PATH 'tools\idf.py'
    & $commandPython $commandTool @args
}
Set-Alias -Name idf.py -Value Invoke-idfpy -Scope Global -Force
if ($Action -eq 'activate') {
    Write-Host "ESP-IDF environment ready: $version"
    Write-Host 'You can now run idf.py commands in this PowerShell terminal.'
    Write-Host "Project directory: $PSScriptRoot"
    return
}
if ($Action -in @('flash', 'monitor') -and -not $Port) { throw '-Port COMx is required.' }
Push-Location $PSScriptRoot
try {
    $configFile = Join-Path $PSScriptRoot 'sdkconfig'
    $buildDir = Join-Path $PSScriptRoot 'build'
    $profileArgs = @()
    $resolvedTuning = if ($TcpTuning -ne 'auto') { $TcpTuning } elseif ($Profile -eq 'throughput') { 'fast' } else { 'baseline' }
    if ($Profile -eq 'throughput' -or $Transport -ne 'auto' -or $TcpTuning -ne 'auto' -or $Experiment -ne 'none') {
        # Isolate all generated configuration/output; never reset user's sdkconfig.
        $name = if ($Transport -eq 'auto') { "build-$Profile-$resolvedTuning-socket" } else { "build-$Profile-$Transport-$resolvedTuning-socket" }
        if ($Experiment -ne 'none') { $name += "-exp-$Experiment" }
        $buildDir = Join-Path $PSScriptRoot $name
        $configFile = Join-Path $buildDir 'sdkconfig'
        New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
        $defaults = Join-Path $PSScriptRoot 'sdkconfig.defaults'
        if ($Profile -eq 'throughput') { $defaults += ';' + (Join-Path $PSScriptRoot 'sdkconfig.throughput.defaults') }
        $defaults += ';' + (Join-Path $PSScriptRoot "sdkconfig.tcp-$resolvedTuning.defaults")
        if ($Experiment -ne 'none') {
            $defaults += ';' + (Join-Path $PSScriptRoot "sdkconfig.experiment-$Experiment.defaults")
            Write-Host "Isolated data-path experiment: $Experiment"
            if ($Experiment -eq 'psram80') {
                Write-Warning '80 MHz is experimental. Confirm PSRAM part rating and PCB margins before flashing.'
            }
        }
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
    # Component Kconfig edits are not always detected by an existing Ninja
    # cache. Refresh generated headers without replacing user's sdkconfig.
    $generatedHeader = Join-Path $buildDir 'config/sdkconfig.h'
    $componentKconfig = Join-Path $PSScriptRoot 'main/Kconfig.projbuild'
    # Experiments always normalize Kconfig before validating it: derived values
    # (e.g. SPIRAM_SPEED) must not be a stale hand-edited sdkconfig entry.
    if ($Experiment -ne 'none' -or -not (Test-Path -LiteralPath $generatedHeader) -or
        (Get-Item -LiteralPath $componentKconfig).LastWriteTimeUtc -gt (Get-Item -LiteralPath $generatedHeader).LastWriteTimeUtc) {
        & $idfPython $idfTool @profileArgs reconfigure
        if ($LASTEXITCODE -ne 0) { throw 'Kconfig refresh failed' }
    }
    # Defaults seed NEW configs; they never silently overwrite an existing
    # sdkconfig. Refuse mislabeled experiment directories rather than flashing
    # a stale/mutated config that would invalidate the A/B comparison.
    if ($Experiment -ne 'none') {
        $checks = @{
            'CONFIG_PSRAM_TCP_DIRECT' = $Experiment -in @('direct','combined','psram80')
            'CONFIG_LWIP_TCPIP_CORE_LOCKING' = $Experiment -in @('core','combined','psram80')
            'CONFIG_LWIP_TCPIP_CORE_LOCKING_INPUT' = $Experiment -in @('core','combined','psram80')
            'CONFIG_LWIP_CHECK_THREAD_SAFETY' = $Experiment -in @('core','combined','psram80')
        }
        foreach ($key in $checks.Keys) {
            $enabled = Select-String -LiteralPath $configFile -Pattern "^$key=y$" -Quiet
            if ($enabled -ne $checks[$key]) { throw "Experiment config mismatch: $key; inspect $configFile (not overwritten)." }
        }
        $speed = if ($Experiment -eq 'psram80') { 80 } else { 40 }
        if (-not (Select-String -LiteralPath $configFile -Pattern "^CONFIG_SPIRAM_SPEED=$speed$" -Quiet)) {
            throw "Experiment config mismatch: expected PSRAM $speed MHz; inspect $configFile (not overwritten)."
        }
    }
    $idfArgs = @($profileArgs)
    if ($Port) { $idfArgs += @('-p', $Port) }
    $idfArgs += $Action
    & $idfPython $idfTool @idfArgs
    if ($LASTEXITCODE -ne 0) { throw "idf.py $Action failed" }
} finally { Pop-Location }
