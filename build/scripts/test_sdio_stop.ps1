param([string]$Compiler='D:/AMDDesignTools/2026.1/tps/mingw/10.0.0/win64.o/nt/bin/gcc.exe')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$out=Join-Path $root ('build/sdio_stop_tests/'+(Get-Date -Format 'yyyyMMdd_HHmmssfff'))
New-Item -ItemType Directory -Force -Path $out | Out-Null
$env:PATH="$(Split-Path $Compiler -Parent);$env:PATH"
$sources=@("$root/esp32c61/main/sdio_bridge.c","$root/esp32c61/main/bridge_crc.c","$root/esp32c61/main/raw_transfer.c")
$flags=@('-std=c11','-Wall','-Wextra','-Werror','-Wpedantic','--coverage','-O0',"-I$root/esp32c61/main","-I$root/esp32c61/tests/stubs")
Push-Location $out
try {
    $objects=@()
    foreach($source in $sources) {
        $object=[IO.Path]::GetFileNameWithoutExtension($source)+'.o'
        & $Compiler @flags -c $source -o $object
        if($LASTEXITCODE){throw 'SDIO stop production object compile failed'}
        $objects+=$object
    }
    & $Compiler @flags "$root/esp32c61/tests/test_sdio_bridge.c" @objects -o legacy.exe
    if($LASTEXITCODE){throw 'SDIO legacy test compile failed'}
    & ./legacy.exe
    if($LASTEXITCODE){throw 'SDIO legacy regression failed'}
    foreach($fault in @('mutex','task','task-init','dma','pool','partial-pool','init','start','register','load','write',
                       'runtime-init','runtime-start','runtime-register','runtime-load')) {
        & ./legacy.exe $fault
        if($LASTEXITCODE){throw "SDIO $fault regression failed"}
    }
    & $Compiler @flags "$root/esp32c61/tests/test_sdio_stop.c" @objects -o stop.exe
    if($LASTEXITCODE){throw 'SDIO stop test compile failed'}
    & ./stop.exe
    if($LASTEXITCODE){throw 'SDIO stop ownership test failed'}
    & $Compiler -std=c11 -Wall -Wextra -Werror -Wpedantic -DDATA_OUTPUT_MODE=1 `
      "-I$root/build/src/ps/data_transfer" "-I$root/esp32c61/tests/stubs/host" `
      -c "$root/esp32c61/tests/test_stop_host_adapter.c" -o host.o
    if($LASTEXITCODE){throw 'PS stop integration adapter compile failed'}
    & $Compiler @flags "$root/esp32c61/tests/test_sdio_stop_integration.c" @objects `
      "$root/build/src/ps/data_transfer/frame_crc.c" host.o -o integration.exe
    if($LASTEXITCODE){throw 'PS/ESP stop integration link failed'}
    & ./integration.exe
    if($LASTEXITCODE){throw 'PS/ESP stop integration failed'}
    & "$(Split-Path $Compiler -Parent)/gcov.exe" -b -c sdio_bridge.gcno
    if($LASTEXITCODE){throw 'SDIO stop combined coverage failed'}
    $core=@('request_reset','reap','sender_owned','cancel_sender','discard_ready','reset_link','begin_stop','poll_stop')
    $function='';$lines=0;$hitLines=0;$branches=0;$hitBranches=0
    foreach($line in Get-Content -LiteralPath sdio_bridge.c.gcov) {
        if($line -match '^function (\w+) '){$function=$Matches[1]}
        if($function -notin $core){continue}
        if($line -match '^\s*([^:]+):\s*\d+:') {
            $count=$Matches[1].Trim()
            if($count -ne '-') {$lines++;if($count -notmatch '#'){$hitLines++}}
        }
        if($line -match '^branch ') {
            $branches++;if($line -notmatch 'taken 0|never executed'){$hitBranches++}
        }
    }
    if(!$lines -or !$branches){throw 'Missing SDIO stop core coverage'}
    $linePct=100.0*$hitLines/$lines;$branchPct=100.0*$hitBranches/$branches
    'STOP_CORE_COVERAGE lines={0:F2}% branches={1:F2}%' -f $linePct,$branchPct
    if($linePct -le 95 -or $branchPct -le 90){throw 'SDIO stop core coverage below target'}
} finally {Pop-Location}
