param([switch]$Execute)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$buildRoot = (Resolve-Path -LiteralPath (Join-Path $projectRoot 'build')).Path
$buildPrefix = $buildRoot.TrimEnd('\') + '\'

# Explicit historical build locations only; never include src, scripts,
# constraints, reports, output as a whole, or the user's current workspace.
$relativeTargets = @(
    'sdio_vitis_20261009_114605267', 'sdio_vitis_20261009_120005488',
    'sdio_vitis_20261011_154744918', 'sdio_vitis_20261011_155051580',
    'sdio_vitis_20261011_191603846', 'sdio_vitis_20261011_192152731',
    'tcp_pipeline_usb', 'verification', 'workspace_backups', 'NA', 'ipython',
    'output/sdio_host_50mhz.d', 'output/sdio_host_50mhz.obj',
    'output/test_actual_formatter.exe', 'output/udp_opt_raw.bin'
)
$targets = @()
$records = @()
foreach ($relative in $relativeTargets) {
    $candidate = [IO.Path]::GetFullPath((Join-Path $buildRoot $relative))
    if (-not $candidate.StartsWith($buildPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Outside build directory: $candidate"
    }
    if (-not (Test-Path -LiteralPath $candidate)) { continue }
    $item = Get-Item -LiteralPath $candidate -Force
    if ($item.FullName -ne $candidate -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Unsafe target: $candidate"
    }
    $targets += $item
    $children = if ($item.PSIsContainer) { @(Get-ChildItem -LiteralPath $candidate -Recurse -Force) } else { @($item) }
    foreach ($child in $children) {
        if ($child.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse point: $($child.FullName)" }
        if (-not $child.FullName.StartsWith($buildPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Unsafe child: $($child.FullName)"
        }
        if (-not $child.PSIsContainer) {
            $records += [pscustomobject]@{
                path = $child.FullName.Substring($buildPrefix.Length).Replace('\', '/')
                bytes = $child.Length
                sha256 = ''
            }
        }
    }
}
$total = ($records | Measure-Object bytes -Sum).Sum
Write-Output "Validated targets: $($targets.Count); files: $($records.Count); bytes: $total"
$targets.FullName | Write-Output
if (-not $Execute) { Write-Output 'Dry run only. Use -Execute to archive, verify and remove these targets.'; return }
if (-not $targets.Count) { return }

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archiveDirectory = Join-Path $buildRoot 'archives'
if (Test-Path -LiteralPath $archiveDirectory) {
    if ((Get-Item -LiteralPath $archiveDirectory -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw 'Archive directory must not be a reparse point.'
    }
}
New-Item -ItemType Directory -Path $archiveDirectory -Force | Out-Null
$archivePath = Join-Path $archiveDirectory ('build_cleanup_' + (Get-Date -Format 'yyyyMMdd_HHmmssfff') + '.zip')
$zip = [IO.Compression.ZipFile]::Open($archivePath, [IO.Compression.ZipArchiveMode]::Create)
try {
    $index = 0
    foreach ($record in $records) {
        $sourcePath = Join-Path $buildRoot $record.path
        $record.sha256 = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $sourcePath, $record.path,
            [IO.Compression.CompressionLevel]::Optimal) | Out-Null
        $index++
        if ($index % 2000 -eq 0) { Write-Output "Archived $index / $($records.Count) files" }
    }
    $manifest = $zip.CreateEntry('cleanup-manifest.json')
    $writer = [IO.StreamWriter]::new($manifest.Open(), [Text.UTF8Encoding]::new($false))
    try {
        $writer.Write((@{ root=$buildRoot; targets=@($targets.FullName); files=$records } | ConvertTo-Json -Depth 6))
    } finally { $writer.Dispose() }
} finally { $zip.Dispose() }

# Verify every archived file and ensure the local source did not change.
# Any mismatch aborts before the first removal; the partial archive is retained.
$zip = [IO.Compression.ZipFile]::OpenRead($archivePath)
try {
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        foreach ($record in $records) {
            $entry = $zip.GetEntry($record.path)
            if (-not $entry -or $entry.Length -ne $record.bytes) { throw "Archive length mismatch: $($record.path)" }
            $stream = $entry.Open()
            try { $digest = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '') }
            finally { $stream.Dispose() }
            $sourceHash = (Get-FileHash -LiteralPath (Join-Path $buildRoot $record.path) -Algorithm SHA256).Hash
            if ($digest -ne $record.sha256 -or $sourceHash -ne $record.sha256) {
                throw "Archive/source checksum mismatch: $($record.path)"
            }
        }
    } finally { $sha.Dispose() }
} finally { $zip.Dispose() }
Write-Output "Archive verified: $archivePath"

# Validate exact absolute targets again immediately before recursive removal.
foreach ($target in $targets) {
    $resolved = (Resolve-Path -LiteralPath $target.FullName).Path
    if ($resolved -ne $target.FullName -or -not $resolved.StartsWith($buildPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing removal of unexpected path: $resolved"
    }
    $entries = if ($target.PSIsContainer) { @(Get-ChildItem -LiteralPath $resolved -Recurse -Force) } else { @() }
    if (@($entries | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) {
        throw "Target changed to contain a reparse point: $resolved"
    }
    $expected = @($records | Where-Object {
        $source = Join-Path $buildRoot $_.path
        $source -eq $resolved -or $source.StartsWith($resolved + '\', [StringComparison]::OrdinalIgnoreCase)
    }).Count
    $actual = if ($target.PSIsContainer) { @($entries | Where-Object { -not $_.PSIsContainer }).Count } else { 1 }
    if ($expected -ne $actual) { throw "Target file count changed: $resolved" }
}
foreach ($target in $targets) {
    Remove-Item -LiteralPath $target.FullName -Recurse -Force
    Write-Output "Removed archived target: $($target.FullName)"
}
$archive = Get-Item -LiteralPath $archivePath
Write-Output "Archive SHA256: $((Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash)"
Write-Output "Archived bytes: $total; ZIP bytes: $($archive.Length); net bytes reclaimed: $($total - $archive.Length)"
