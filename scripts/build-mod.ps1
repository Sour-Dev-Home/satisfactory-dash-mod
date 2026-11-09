# Builds the core mod (mod/SatisDashAgent) for dedicated servers and unzips the archived plugin to -Out.
# Same steps and command line as build-spike.ps1 (Alpakit's UAT PackagePlugin; the sources are cited there). The
# duplication is deliberate: build-spike.ps1's ValidatePattern and paths are what its passing runs used.
# -Platforms Win64 (the default) unzips the Windows server's plugin to -Out itself, as every core run expects.
# -Platforms Win64+Linux also builds the Linux server (Alpakit joins server platforms with "+", AlpakitProfile.cpp:49)
# and asks for Alpakit's merged archive too (-merge, AlpakitProfile.cpp:89): PackagePlugin then writes <plugin>.zip with
# one folder per platform (WindowsServer, LinuxServer) next to the per-platform zips (Alpakit.Automation/PackagePlugin.cs:45-47,
# 186-213). That merged zip, the multi-target file a release ships, is what's unzipped to -Out (-Out\WindowsServer,
# -Out\LinuxServer), so `harness check-build` judges the shipped file. All three zips are copied to -Out\zips.
# -DryRun prints the PackagePlugin command line and builds nothing.
param(
	[Parameter(Mandatory)] [string] $Project,
	[Parameter(Mandatory)] [string] $Engine,
	[Parameter(Mandatory)] [string] $Out,
	[ValidateSet('Win64', 'Win64+Linux')] [string] $Platforms = 'Win64',
	[switch] $DryRun
)
$ErrorActionPreference = 'Stop'
# Same reason as build-spike.ps1: the SML project's pre-build step runs a PowerShell script.
$env:PSExecutionPolicyPreference = 'RemoteSigned'
$plugin = 'SatisDashAgent'
$repo = Split-Path $PSScriptRoot -Parent
$source = Join-Path $repo "mod\SatisDashAgent"
$uproject = Join-Path $Project 'FactoryGame.uproject'
$uatArgs = @("-ScriptsForProject=$uproject", 'PackagePlugin', "-project=$uproject", '-clientconfig=Shipping',
	'-serverconfig=Shipping', '-utf8output', "-DLCName=$plugin", '-build', '-server', "-serverplatform=$Platforms",
	'-noclient', '-nocompileeditor', '-installed')
$merged = $Platforms -eq 'Win64+Linux'
if ($merged) { $uatArgs += '-merge' }
if ($DryRun) {
	Write-Output "RunUAT $($uatArgs -join ' ')"
	exit 0
}
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

& (Join-Path $Engine 'Engine\Build\BatchFiles\RunUAT.bat') @uatArgs
if ($LASTEXITCODE -ne 0) { throw "PackagePlugin failed ($LASTEXITCODE)" }

# One archive per server platform, named <plugin>-<platform>.zip (SatisDashAgent-WindowsServer.zip), plus <plugin>.zip
# when merged.
$zips = @(Get-ChildItem $archive -Filter *.zip)
$platformNames = @('WindowsServer') + $(if ($merged) { @('LinuxServer') } else { @() })
$names = @($platformNames | ForEach-Object { "$plugin-$_.zip" }) + $(if ($merged) { @("$plugin.zip") } else { @() })
$found = @($zips | ForEach-Object Name | Sort-Object)
if ((Compare-Object $found ($names | Sort-Object))) { throw "expected archived zip(s) $($names -join ', ') in $archive, found $($found -join ', ')" }
if (Test-Path $Out) { Remove-Item -Recurse -Force $Out }
if ($merged) {
	# The merged zip holds a folder per platform; unzipped to -Out it is the tree check-build judges.
	Expand-Archive -Path (Join-Path $archive "$plugin.zip") -DestinationPath $Out
	$top = @(Get-ChildItem $Out -Force | ForEach-Object Name | Sort-Object)
	if ((Compare-Object $top ($platformNames | Sort-Object))) { throw "$plugin.zip holds $($top -join ', '), expected $($platformNames -join ', ')" }
	foreach ($p in $platformNames) {
		if (-not (Test-Path (Join-Path $Out "$p\$plugin.uplugin"))) { throw "$plugin.zip has no $p\$plugin.uplugin" }
	}
	$zipsOut = Join-Path $Out 'zips'
	New-Item -ItemType Directory -Path $zipsOut | Out-Null
	foreach ($zip in $zips) { Copy-Item $zip.FullName $zipsOut }
} else {
	Expand-Archive -Path $zips[0].FullName -DestinationPath $Out
	if (-not (Test-Path (Join-Path $Out "$plugin.uplugin"))) { throw "$($zips[0].Name) has no $plugin.uplugin at its root" }
}
# S14: the test module is Editor-only and must never ship in a server package.
$tests = @(Get-ChildItem $Out -Recurse -File | Where-Object { $_.Name -like '*SatisDashCoreTests*' })
if ($tests.Count -ne 0) { throw "the package carries test code: $($tests[0].Name)" }
Write-Output "built $plugin -> $Out ($(($zips | ForEach-Object Name) -join ', '))"
