param(
    [Parameter(Mandatory=$true)][string]$Elf,
    [string]$Tools='D:/AMDDesignTools/2026.1'
)
$ErrorActionPreference='Stop'
$dump=& "$Tools/Vitis/gnu/aarch32/nt/gcc-arm-none-eabi/bin/arm-none-eabi-objdump.exe" -h $Elf
if($LASTEXITCODE) {throw 'Cannot inspect ELF sections'}
$end=0L;$sections=0
for($i=0;$i -lt $dump.Count-1;$i++) {
    if($dump[$i] -match '^\s*\d+\s+(\S+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+' -and
       $dump[$i+1] -match '\bALLOC\b') {
        # Re-match: the ALLOC predicate has its own match state.
        $null=$dump[$i] -match '^\s*\d+\s+(\S+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+'
        $name=$Matches[1]
        $size=[Convert]::ToInt64($Matches[2],16)
        $start=[Convert]::ToInt64($Matches[3],16)
        if(!$size) {continue}
        if($start -lt 0x100000 -or $start+$size -gt 0x1c000000) {
            throw "ALLOC section $name escapes application DDR; USB/frame reservations are not safe"
        }
        $end=[Math]::Max($end,$start+$size);$sections++
    }
}
if(!$sections) {throw 'No allocated sections found; refusing an unverified ELF'}
'ELF_DDR_OK: {0}; {1} sections; end=0x{2:X8}; USB=0x1C000000; frames=0x20000000..0x202FFFFF' -f $Elf,$sections,$end
