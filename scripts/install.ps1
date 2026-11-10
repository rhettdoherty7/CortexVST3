# Copy the built Cortex.vst3 into the system VST3 folder so FL Studio can find it.
# The folder is under Program Files, so run this from an elevated (Administrator) PowerShell:
#
#   powershell -ExecutionPolicy Bypass -File scripts\install.ps1
#
# Then in FL Studio: Options > Manage plugins > Find installed plugins.

param([ValidateSet('Release', 'Debug')] [string] $Config = 'Release')

$ErrorActionPreference = 'Stop'
$repo   = Split-Path -Parent $PSScriptRoot
$source = Join-Path $repo "build\src\Cortex_artefacts\$Config\VST3\Cortex.vst3"
$target = Join-Path $env:CommonProgramFiles 'VST3'

if (-not (Test-Path $source)) { throw "Cortex.vst3 not built yet. Run scripts\build.ps1 first." }

Copy-Item -Path $source -Destination $target -Recurse -Force
Write-Host "Installed Cortex.vst3 to $target"
