param([string]$OutputDirectory = "$PSScriptRoot/artifacts/WorkTool-Windows-x64")
$ErrorActionPreference = 'Stop'
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
node "$PSScriptRoot/sync-rules.cjs"
if ($LASTEXITCODE -ne 0) { throw 'Rule sync failed' }
dotnet run --project "$PSScriptRoot/WorkTool.Tests/WorkTool.Tests.csproj" -c Release
if ($LASTEXITCODE -ne 0) { throw 'Native regression checks failed' }
dotnet publish "$PSScriptRoot/WorkTool.WinUI/WorkTool.WinUI.csproj" -c Release -p:Platform=x64 -p:UseSharedCompilation=false -o $OutputDirectory
if ($LASTEXITCODE -ne 0) { throw 'Publish failed' }
Copy-Item "$PSScriptRoot/README.md" "$OutputDirectory/README.md"
Write-Host "Run $OutputDirectory/WorkTool.WinUI.exe"
