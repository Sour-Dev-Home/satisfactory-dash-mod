# Builds the core mod (mod/SatisDashAgent) into the SML project's editor and runs its automation tests headless.
# The command is UE's own: "Automation RunTests <filter>; Quit" (Engine/Source/Developer/AutomationController/Private/
# AutomationCommandline.cpp:580, :696) quits once the queue is empty (:474), and -ReportExportPath= writes index.json
# there (AutomationControllerManager.cpp:194, :672). harness judge-mod-test reads it.
param(
	[Parameter(Mandatory)] [string] $Project,
	[Parameter(Mandatory)] [string] $Engine,
	[Parameter(Mandatory)] [string] $Out,
	[string] $Filter = 'SatisDash.'
)
$ErrorActionPreference = 'Stop'
# Same reason as build-spike.ps1: the SML project's pre-build step runs a PowerShell script.
$env:PSExecutionPolicyPreference = 'RemoteSigned'
$plugin = 'SatisDashAgent'
$repo = Split-Path $PSScriptRoot -Parent
$source = Join-Path $repo "mod\$plugin"
$uproject = Join-Path $Project 'FactoryGame.uproject'
if (-not (Test-Path "$source\$plugin.uplugin")) { throw "no plugin at $source" }
if (-not (Test-Path $uproject)) { throw "no SML project at $uproject" }

# FactoryEditor builds every plugin in Mods; spike plugins left there would build too.
Get-ChildItem (Join-Path $Project 'Mods') -Directory -Filter 'SatisMod*' | Remove-Item -Recurse -Force
$target = Join-Path $Project "Mods\$plugin"
robocopy $source $target /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE)" }

& (Join-Path $Engine 'Engine\Build\BatchFiles\Build.bat') FactoryEditor Win64 Development "-project=$uproject" -WaitMutex -NoHotReload
if ($LASTEXITCODE -ne 0) { throw "FactoryEditor build failed ($LASTEXITCODE)" }

if (Test-Path $Out) { Remove-Item -Recurse -Force $Out }
New-Item -ItemType Directory -Path $Out | Out-Null
& (Join-Path $Engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') $uproject "-ExecCmds=Automation RunTests $Filter; Quit" `
	"-ReportExportPath=$Out" -unattended -nullrhi -nosplash -nosound -nop4 -stdout -FullStdOutLogOutput
if ($LASTEXITCODE -ne 0) { throw "the editor exited with $LASTEXITCODE" }
if (-not (Test-Path (Join-Path $Out 'index.json'))) { throw "no index.json in $Out" }
Write-Output "tests ran -> $(Join-Path $Out 'index.json')"
