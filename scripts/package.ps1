$ErrorActionPreference = 'Stop'
$repoPath = Split-Path $PSScriptRoot -Parent
$distPath = Join-Path $repoPath 'dist'
$stagePath = Join-Path $distPath ('CantoDeck-0.1.0-win-x64-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path "$stagePath/source", "$stagePath/licenses" -Force | Out-Null
Copy-Item -LiteralPath "$repoPath/build/CantoDeck_artefacts/Release/CantoDeck.exe" -Destination $stagePath
foreach ($name in @('README.md','README.vi.md','LICENSE','THIRD_PARTY_NOTICES.md','CONTRIBUTING.md','SECURITY.md','CODE_OF_CONDUCT.md')) { Copy-Item -LiteralPath (Join-Path $repoPath $name) -Destination $stagePath }
Copy-Item -LiteralPath "$repoPath/docs" -Destination $stagePath -Recurse
$jucePath = Join-Path $repoPath 'vendor/JUCE'
if (!(Test-Path "$jucePath/CMakeLists.txt")) { $jucePath = Join-Path $repoPath 'build/_deps/juce-src' }
$revision = & git -C $jucePath rev-parse HEAD
if ($revision -ne '91ad83ae34a81e0833b1a2b0866f54846370ae53') { throw 'JUCE revision does not match pin' }
& git -C $jucePath archive --format=zip --output="$stagePath/source/JUCE-8.0.15.zip" HEAD
if ($LASTEXITCODE) { throw 'JUCE source archive failed' }
$sourcePaths = @('app','engine','platform','tests','scripts','docs','.github','CMakeLists.txt','.gitignore','.gitattributes','.clang-format','README.md','README.vi.md','CONTRIBUTING.md','SECURITY.md','CODE_OF_CONDUCT.md','LICENSE','THIRD_PARTY_NOTICES.md') | ForEach-Object { Join-Path $repoPath $_ }
Compress-Archive -LiteralPath $sourcePaths -DestinationPath "$stagePath/source/CantoDeck-0.1.0-source.zip"
$notices = @{
 'JUCE-LICENSE.md'='LICENSE.md';
 'zlib-LICENSE.txt'='modules/juce_core/zip/zlib/LICENSE';
 'libpng-LICENSE.txt'='modules/juce_graphics/image_formats/pnglib/LICENSE';
 'JPEG-README.txt'='modules/juce_graphics/image_formats/jpglib/README';
 'HarfBuzz-COPYING.txt'='modules/juce_graphics/fonts/harfbuzz/COPYING';
 'SheenBidi-LICENSE.txt'='modules/juce_graphics/unicode/sheenbidi/LICENSE'
}
foreach ($name in $notices.Keys) { Copy-Item -LiteralPath (Join-Path $jucePath $notices[$name]) -Destination "$stagePath/licenses/$name" }
$archivePath = "$stagePath.zip"
Compress-Archive -LiteralPath $stagePath -DestinationPath $archivePath
$hash = Get-FileHash -LiteralPath $archivePath -Algorithm SHA256
$hashText = "$($hash.Hash.ToLower())  $([IO.Path]::GetFileName($archivePath))"
[IO.File]::WriteAllText("$archivePath.sha256", $hashText + [Environment]::NewLine)
Write-Output $archivePath
Write-Output $hashText
