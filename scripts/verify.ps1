$ErrorActionPreference = 'Stop'
$repoPath = Split-Path $PSScriptRoot -Parent
$evidencePath = Join-Path $repoPath ('build/evidence-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $evidencePath | Out-Null
$exe = Join-Path $repoPath 'build/CantoDeck_artefacts/Release/CantoDeck.exe'
& "$repoPath/build/Release/core_tests.exe"
if ($LASTEXITCODE) { throw 'Core tests failed' }
& "$repoPath/build/Release/core_tests.exe" --fixture "$evidencePath/input.wav"
if ($LASTEXITCODE) { throw 'Fixture generation failed' }
$process = Start-Process -FilePath $exe -ArgumentList @('--render', ('"' + "$evidencePath/input.wav" + '"'), ('"' + "$evidencePath/render.wav" + '"')) -PassThru -Wait -WindowStyle Hidden
if ($process.ExitCode) { throw "Offline render failed: $($process.ExitCode)" }
foreach ($name in @('render.wav','render-dry.wav','render-wet.wav')) {
    $filePath = Join-Path $evidencePath $name
    if ((Get-Item $filePath).Length -lt 100000) { throw "Recording too small: $name" }
    $reader = [IO.BinaryReader]::new([IO.File]::OpenRead($filePath))
    try {
        $reader.BaseStream.Position = 12
        [double]$peak = 0.0
        while ($reader.BaseStream.Position -lt $reader.BaseStream.Length) {
            $chunkName = [Text.Encoding]::ASCII.GetString($reader.ReadBytes(4))
            $chunkLength = $reader.ReadUInt32()
            if ($chunkName -eq 'data') {
                for ($index = 0; $index -lt $chunkLength; $index += 3) {
                    $value = [int]$reader.ReadByte() -bor ([int]$reader.ReadByte() -shl 8) -bor ([int]$reader.ReadByte() -shl 16)
                    if ($value -ge 8388608) { $value -= 16777216 }
                    $peak = [Math]::Max($peak, [Math]::Abs($value / 8388608.0))
                }
                break
            }
            $reader.BaseStream.Position += $chunkLength + ($chunkLength % 2)
        }
        if ($peak -lt 0.001) { throw "Fixture render unexpectedly silent: $name" }
        if ($name -eq 'render.wav' -and $peak -gt 0.951) { throw 'Output protection exceeded' }
        Write-Output "$name peak: $peak"
    } finally { $reader.Dispose() }
}
$process = Start-Process -FilePath $exe -ArgumentList @('--diagnostics', ('"' + "$evidencePath/diagnostics.txt" + '"')) -PassThru -Wait -WindowStyle Hidden
if ($process.ExitCode) { throw 'Diagnostics failed' }
$process = Start-Process -FilePath $exe -ArgumentList @('--ui-smoke', ('"' + "$evidencePath/ui.png" + '"')) -PassThru -Wait -WindowStyle Hidden
if ($process.ExitCode) { throw "UI/session/preset smoke failed: $($process.ExitCode)" }
foreach ($name in @('ui.png', 'ui-advanced.png', 'ui-eq.png')) {
    if (!(Test-Path -LiteralPath "$evidencePath/$name") -or (Get-Item -LiteralPath "$evidencePath/$name").Length -eq 0) {
        throw "UI smoke did not produce $name"
    }
}
Write-Output 'Verified UI snapshots and session/preset round-trip (no audio streams, no session writes)'
Write-Output "Verified offline graph, mix/dry/wet WAV finalization and injected-fault silence: $evidencePath"
