param(
    [string]$Compiler='D:/AMDDesignTools/2026.1/tps/mingw/10.0.0/win64.o/nt/bin/gcc.exe',
    [string]$OutputDirectory=''
)
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if(!$OutputDirectory) {$OutputDirectory=Join-Path $root 'build/verification/usb_frame_host'}
$env:PATH="$(Split-Path $Compiler);$env:PATH"
foreach($case in @('frame','usb','sdio','bsp_timer')) {
    $out=Join-Path $OutputDirectory ((Get-Date -Format 'yyyyMMdd-HHmmssfff')+'-'+$case)
    New-Item -ItemType Directory -Path $out -Force | Out-Null
    Push-Location $out
    try {
        $flags=@('-std=c11','-Wall','-Wextra','-Werror','-Wpedantic','--coverage','-O0','-g')
        if($case -eq 'frame') {
            $test=Join-Path $root 'esp32c61/tests/test_usb_frame_tx.c'
        } else {
            $mode=if($case -eq 'sdio'){1}else{0}
            $flags+=@("-DDATA_OUTPUT_MODE=$mode",'-I',"$root/esp32c61/tests/usb_hw_stubs",'-I',"$root/esp32c61/tests/stubs/temperature")
            if($case -eq 'bsp_timer') {$flags+='-DTEMPERATURE_TEST_UNPAREN_FREQ'}
            $test=Join-Path $root 'esp32c61/tests/test_usb_transport.c'
        }
        & $Compiler @flags $test "$root/build/src/ps/usb/usb_frame_tx.c" -o test.exe
        if($LASTEXITCODE) {throw "$case compile failed"}
        & ./test.exe
        if($LASTEXITCODE) {throw "$case assertions failed"}
        foreach($gcno in Get-ChildItem -Filter '*.gcno') {
            & "$(Split-Path $Compiler)/gcov.exe" -b -c $gcno.Name
            if($LASTEXITCODE) {throw "$case coverage failed"}
        }
    } finally {Pop-Location}
}
