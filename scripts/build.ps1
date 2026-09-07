param([string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$repoPath = Split-Path $PSScriptRoot -Parent
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) { $cmakePath = $cmakeCommand.Source } else {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $cmakePath = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
& $cmakePath -S $repoPath -B "$repoPath/build" -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE) { throw 'Configure failed' }
& $cmakePath --build "$repoPath/build" --config $Configuration --parallel 4
if ($LASTEXITCODE) { throw 'Build failed' }
$ctestPath = Join-Path (Split-Path $cmakePath) 'ctest.exe'
& $ctestPath --test-dir "$repoPath/build" -C $Configuration --output-on-failure
if ($LASTEXITCODE) { throw 'Tests failed' }
