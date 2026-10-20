# Builds the core mod (mod/SatisDashAgent) for a Windows dedicated server and unzips the archived plugin to -Out.
# Same steps and command line as build-spike.ps1 (Alpakit's UAT PackagePlugin; the sources are cited there). The
# duplication is deliberate: build-spike.ps1's ValidatePattern and paths are what its passing runs used.
param(
	[Parameter(Mandatory)] [string] $Project,
	[Parameter(Mandatory)] [string] $Engine,
	[Parameter(Mandatory)] [string] $Out
)
$ErrorActionPreference = 'Stop'
# Same reason as build-spike.ps1: the SML project's pre-build step runs a PowerShell script.
$env:PSExecutionPolicyPreference = 'RemoteSigned'
$plugin = 'SatisDashAgent'
$repo = Split-Path $PSScriptRoot -Parent
$source = Join-Path $repo "mod\SatisDashAgent"
$uproject = Join-Path $Project 'FactoryGame.uproject'
if (-not (Test-Path "$source\$plugin.uplugin")) { throw "no plugin at $source" }
if (-not (Test-Path $uproject)) { throw "no SML project at $uproject" }

# FactoryEditor builds every plugin in Mods, so a spike left there could break this build. The core mod stays (and
# is mirrored below).
Get-ChildItem (Join-Path $Project 'Mods') -Directory -Filter 'SatisMod*' | Remove-Item -Recurse -Force

# Mirror the plugin into the project's Mods folder; /MIR also deletes files removed from the source.
$target = Join-Path $Project "Mods\$plugin"
robocopy $source $target /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE)" }

# The cook runs the editor, which must load the plugin's editor modules; PackagePlugin's -nocompileeditor won't build
# them (build-spike.ps1).
& (Join-Path $Engine 'Engine\Build\BatchFiles\Build.bat') FactoryEditor Win64 Development "-project=$uproject" -WaitMutex -NoHotReload
if ($LASTEXITCODE -ne 0) { throw "FactoryEditor build failed ($LASTEXITCODE)" }

$archive = Join-Path $Project "Saved\ArchivedPlugins\$plugin"
if (Test-Path $archive) { Remove-Item -Recurse -Force $archive }

& (Join-Path $Engine 'Engine\Build\BatchFiles\RunUAT.bat') "-ScriptsForProject=$uproject" PackagePlugin "-project=$uproject" `
	-clientconfig=Shipping -serverconfig=Shipping -utf8output "-DLCName=$plugin" -build -server -serverplatform=Win64 `
	-noclient -nocompileeditor -installed
if ($LASTEXITCODE -ne 0) { throw "PackagePlugin failed ($LASTEXITCODE)" }

$zips = @(Get-ChildItem $archive -Filter *.zip)
if ($zips.Count -ne 1) { throw "expected one archived zip in $archive, found $($zips.Count)" }
if (Test-Path $Out) { Remove-Item -Recurse -Force $Out }
Expand-Archive -Path $zips[0].FullName -DestinationPath $Out
if (-not (Test-Path (Join-Path $Out "$plugin.uplugin"))) { throw "the archive has no $plugin.uplugin at its root" }
# S14: the test module is Editor-only and must never ship in a server package.
$tests = @(Get-ChildItem $Out -Recurse -File | Where-Object { $_.Name -like '*SatisDashCoreTests*' })
if ($tests.Count -ne 0) { throw "the package carries test code: $($tests[0].Name)" }
Write-Output "built $plugin -> $Out ($($zips[0].Name))"
