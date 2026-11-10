# Configure, build and test Cortex.
#
#   powershell -ExecutionPolicy Bypass -File scripts\build.ps1                 # Release build + unit tests
#   powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Validate      # + pluginval at strictness 10
#   powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Config Debug
#
# pluginval is looked up on PATH, then in tools\pluginval\, then in the sibling Interlock checkout.

param(
    [ValidateSet('Release', 'Debug')] [string] $Config = 'Release',
    [switch] $Validate
)

$ErrorActionPreference = 'Stop'
$repo  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $repo 'build'

$configureArgs = @('-S', $repo, '-B', $build, '-G', 'Visual Studio 17 2022', '-A', 'x64')

# Reuse Interlock's JUCE / Catch2 checkouts when they are there, instead of downloading again.
$interlockDeps = Join-Path (Split-Path -Parent $repo) 'Interlock\build\_deps'
if (Test-Path (Join-Path $interlockDeps 'juce-src'))   { $configureArgs += "-DFETCHCONTENT_SOURCE_DIR_JUCE=$interlockDeps\juce-src" }
if (Test-Path (Join-Path $interlockDeps 'catch2-src')) { $configureArgs += "-DFETCHCONTENT_SOURCE_DIR_CATCH2=$interlockDeps\catch2-src" }

cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }

cmake --build $build --config $Config -- '-m' '-v:m' '-nologo'
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }

ctest --test-dir $build -C $Config --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Unit tests failed' }

if ($Validate) {
    $candidates = @(@(
        (Get-Command pluginval -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source),
        (Join-Path $repo 'tools\pluginval\pluginval.exe'),
        (Join-Path (Split-Path -Parent $repo) 'Interlock\tools\pluginval\pluginval.exe')
    ) | Where-Object { $_ -and (Test-Path $_) })

    if (-not $candidates) { throw 'pluginval not found. Unzip it into tools\pluginval\.' }

    $vst3 = Join-Path $build "src\Cortex_artefacts\$Config\VST3\Cortex.vst3"
    & $candidates[0] --strictness-level 10 --validate-in-process --output-dir (Join-Path $build 'pluginval-logs') $vst3
    if ($LASTEXITCODE -ne 0) { throw 'pluginval failed' }
}

Write-Host "Cortex $Config build OK"
