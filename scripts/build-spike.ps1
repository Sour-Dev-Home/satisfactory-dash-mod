# Builds one G2 spike for a Windows dedicated server with Alpakit's UAT PackagePlugin command, and unzips the
# archived plugin to -Out. Command line from Alpakit (Mods/Alpakit/Source/Alpakit/Private/AlpakitProfile.cpp:47-87)
# and SML's CI (.github/workflows/build.yml:192). Output location from PackagePlugin.cs:215-219.
param(
	[Parameter(Mandatory)] [ValidatePattern('^S[0-9][a-z]?$')] [string] $Spike,
	[Parameter(Mandatory)] [string] $Project,
	[Parameter(Mandatory)] [string] $Engine,
	[Parameter(Mandatory)] [string] $Out
)
$ErrorActionPreference = 'Stop'
$plugin = "SatisMod$Spike"
$repo = Split-Path $PSScriptRoot -Parent
$source = Join-Path $repo "spikes\$Spike\$plugin"
$uproject = Join-Path $Project 'FactoryGame.uproject'
if (-not (Test-Path "$source\$plugin.uplugin")) { throw "no spike plugin at $source" }
if (-not (Test-Path $uproject)) { throw "no SML project at $uproject" }

# FactoryEditor builds every plugin in Mods, so another spike left there (stale, or since deleted from the repo)
# could break this build. Only the spike being built stays.
Get-ChildItem (Join-Path $Project 'Mods') -Directory -Filter 'SatisMod*' |
	Where-Object Name -ne $plugin | Remove-Item -Recurse -Force

# Mirror the spike into the project's Mods folder; /MIR also deletes files removed from the source.
$target = Join-Path $Project "Mods\$plugin"
robocopy $source $target /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE)" }

# The cook runs the editor, which must load the spike's editor module, and PackagePlugin's -nocompileeditor won't
# build it. SML's CI builds FactoryEditor first for the same reason (.github/workflows/build.yml:185).
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
Write-Output "built $plugin -> $Out ($($zips[0].Name))"
