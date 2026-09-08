param(
    [Parameter(Mandatory = $true)][string]$Executable,
    [Parameter(Mandatory = $true)][string]$ReportPath
)
$ErrorActionPreference = 'Stop'
if (Test-Path -LiteralPath $ReportPath) { throw 'Runtime report destination already exists' }
$dumpbinCommand = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
$dumpbinPath = if ($dumpbinCommand) { $dumpbinCommand.Source } else { $null }
if (!$dumpbinPath) {
    $vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (!(Test-Path -LiteralPath $vswherePath)) { throw 'Cannot locate dumpbin: Visual Studio tools required' }
    $dumpbinPath = & $vswherePath -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe' | Select-Object -First 1
}
if (!$dumpbinPath) { throw 'dumpbin.exe not found' }
$dump = & $dumpbinPath /dependents $Executable
if ($LASTEXITCODE) { throw "Dependency inspection failed: $LASTEXITCODE" }
$dependencies = @($dump | ForEach-Object {
    if ($_ -match '^\s+([A-Za-z0-9_.-]+\.dll)\s*$') { $matches[1].ToLowerInvariant() }
} | Sort-Object -Unique)
if ($dependencies -notcontains 'kernel32.dll') { throw 'Could not parse executable dependencies' }
$externalRuntime = @($dependencies | Where-Object { $_ -match '^(msvcp\d.*|vcruntime\d.*|concrt\d.*|msvcr\d.*|ucrtbased)\.dll$' })
if ($externalRuntime.Count) { throw "Portable executable imports external VC runtime: $($externalRuntime -join ', ')" }
@('PASS: no imported MSVC redistributable DLLs.',
  'This checks PE imports, not clean-machine runtime or hardware compatibility.',
  "Executable SHA256: $((Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash.ToLowerInvariant())",
  'Imported DLLs:') + $dependencies | Set-Content -LiteralPath $ReportPath -Encoding UTF8
Write-Output "Verified static VC runtime imports: $ReportPath"
