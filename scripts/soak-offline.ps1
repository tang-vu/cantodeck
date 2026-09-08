param([ValidateRange(1, 1800)][int]$Seconds = 1800)
$ErrorActionPreference = 'Stop'
$repoPath = Split-Path $PSScriptRoot -Parent
$evidencePath = Join-Path $repoPath ('build/offline-soak-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
$exePath = Join-Path $repoPath 'build/CantoDeck_artefacts/Release/CantoDeck.exe'
$fixtureExe = Join-Path $repoPath 'build/Release/core_tests.exe'
if (!(Test-Path -LiteralPath $exePath) -or !(Test-Path -LiteralPath $fixtureExe)) {
    throw 'Build first with scripts/build.ps1'
}
New-Item -ItemType Directory -Path $evidencePath | Out-Null
$sourcePath = Join-Path $evidencePath 'input.wav'
$outputPath = Join-Path $evidencePath 'mix.wav'
& $fixtureExe --fixture-long $sourcePath $Seconds
if ($LASTEXITCODE) { throw "Long fixture failed: $LASTEXITCODE" }
$started = [DateTime]::UtcNow
$process = Start-Process -FilePath $exePath -ArgumentList @('--render', ('"' + $sourcePath + '"'), ('"' + $outputPath + '"')) -WindowStyle Hidden -PassThru
Write-Output "Offline DSP/recording soak PID $($process.Id); evidence: $evidencePath"
$process.WaitForExit()
$process.Refresh()
if ($process.ExitCode) { throw "Long render failed: $($process.ExitCode); retain evidence in $evidencePath" }
$elapsed = ([DateTime]::UtcNow - $started).TotalSeconds
$files = @()
foreach ($name in @('mix.wav', 'mix-dry.wav', 'mix-wet.wav')) {
    $filePath = Join-Path $evidencePath $name
    $files += [ordered]@{
        name = $name
        bytes = (Get-Item -LiteralPath $filePath).Length
        sha256 = (Get-FileHash -LiteralPath $filePath -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}
$report = [ordered]@{
    kind = 'accelerated offline synthetic microphone DSP and mix/dry/wet recording'
    executableSha256 = (Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash.ToLowerInvariant()
    startedUtc = $started.ToString('o')
    renderWallSeconds = $elapsed
    sourceSeconds = $Seconds
    sampleRate = 48000
    expectedFramesPerRecording = [long](($Seconds + 2) * 48000)
    exitCode = $process.ExitCode
    verification = 'Renderer checks recorder failure/frame count, WAV channel/frame headers, mute and injected-fault silence. Checksums identify files; full long-file sample content is not independently compared.'
    limitations = 'No audio streams opened. Not live-clock scheduling, backing-player endurance, listening, physical RTT or hardware acceptance.'
    files = $files
}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $evidencePath 'result.json') -Encoding UTF8
Write-Output "PASS: offline $Seconds-second source plus two-second tail; wall time $([Math]::Round($elapsed, 2)) seconds. Report: $evidencePath/result.json"
