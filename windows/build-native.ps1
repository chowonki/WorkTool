param(
    [string]$OutputDirectory = "$PSScriptRoot/artifacts/WorkTool-Windows-x64",
    [string]$QuickJsSource,
    [switch]$SkipSmoke
)

$ErrorActionPreference = 'Stop'
$OutputDirectory = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
$repo = Split-Path $PSScriptRoot
$project = Join-Path $PSScriptRoot 'WorkTool.Native'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsPath) { throw 'Visual Studio with the Desktop development with C++ workload is required.' }
$msbuild = Join-Path $vsPath 'MSBuild/Current/Bin/amd64/MSBuild.exe'
$cmake = Join-Path $vsPath 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
if (!(Test-Path -LiteralPath $cmake)) { $cmake = (Get-Command cmake.exe -ErrorAction Stop).Source }
$vsVersion = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion
$generator = if ($vsVersion.StartsWith('18.')) { 'Visual Studio 18 2026' } else { 'Visual Studio 17 2022' }

& dotnet restore "$project/Dependencies.proj" --ignore-failed-sources -p:NuGetAudit=false
if ($LASTEXITCODE -ne 0) { throw 'Windows App SDK package restore failed.' }
$assets = Get-Content "$project/obj/project.assets.json" -Raw | ConvertFrom-Json
$nugetRoot = $assets.packageFolders.PSObject.Properties.Name | Select-Object -First 1

$quickBuild = Join-Path $PSScriptRoot 'artifacts/quickjs-build'
$cmakeArguments = @('-S', "$repo/windows-flutter/native", '-B', $quickBuild, '-G', $generator, '-A', 'x64')
if ($QuickJsSource) {
    $QuickJsSource = (Resolve-Path -LiteralPath $QuickJsSource).Path
    $cmakeArguments += "-DFETCHCONTENT_SOURCE_DIR_QUICKJS_NG=$QuickJsSource"
} else {
    $cmakeArguments += '-DFETCHCONTENT_SOURCE_DIR_QUICKJS_NG='
    $QuickJsSource = Join-Path $quickBuild '_deps/quickjs_ng-src'
}
& $cmake @cmakeArguments
if ($LASTEXITCODE -ne 0) { throw 'QuickJS configuration failed.' }
& $cmake --build $quickBuild --config Release
if ($LASTEXITCODE -ne 0) { throw 'QuickJS build failed.' }

& $msbuild "$project/WorkTool.Native.vcxproj" /m /nologo /v:minimal /p:Configuration=Release /p:Platform=x64 "/p:NuGetPackageRoot=$nugetRoot" "/p:OutDir=$OutputDirectory/"
if ($LASTEXITCODE -ne 0) { throw 'WorkTool C++ build failed.' }

$resources = Join-Path $PSScriptRoot 'WorkTool.WinUI/Assets'
$rules = Join-Path $OutputDirectory 'Rules'
$licenses = Join-Path $OutputDirectory 'Licenses'
New-Item -ItemType Directory -Force $rules, $licenses | Out-Null
Copy-Item "$repo/mac/Sources/WorkToolCore/Resources/js/*.js" $rules
Copy-Item "$repo/windows-flutter/assets/js/host.js" $rules
Copy-Item "$repo/CHANGELOG.md", "$resources/WorkTool.ico", "$quickBuild/Release/worktool_js.dll" $OutputDirectory
Copy-Item "$QuickJsSource/LICENSE" "$licenses/QuickJS-NG.txt"
$crt = Get-ChildItem "$vsPath/VC/Redist/MSVC" -Filter Microsoft.VC*.CRT -Directory -Recurse |
    Where-Object { $_.Parent.Name -eq 'x64' -and $_.Parent.Parent.Parent.Name -eq 'MSVC' } |
    Sort-Object { [version]$_.Parent.Parent.Name } -Descending | Select-Object -First 1
if (!$crt) { throw 'The x64 Visual C++ redistributable files are required.' }
Copy-Item "$($crt.FullName)/*.dll" $OutputDirectory
foreach ($file in 'WorkTool.WinUI.exe', 'Microsoft.UI.Xaml.dll', 'resources.pri', 'worktool_js.dll', 'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll') {
    if (!(Test-Path -LiteralPath (Join-Path $OutputDirectory $file))) { throw "Missing app file: $file" }
}
if (!$SkipSmoke) {
    $smoke = Join-Path $PSScriptRoot 'artifacts/native-smoke'
    $arguments = '--smoke-test', ('"{0}"' -f $smoke), ('"{0}"' -f $repo)
    $process = Start-Process -WindowStyle Hidden -FilePath (Join-Path $OutputDirectory 'WorkTool.WinUI.exe') -ArgumentList $arguments -PassThru
    if (!$process.WaitForExit(60000)) { $process.Kill(); throw 'Native smoke test timed out.' }
    Get-Content "$smoke/smoke-result.txt" -Encoding UTF8
    if ($process.ExitCode -ne 0) { throw "Native smoke test failed ($($process.ExitCode))." }
}
Write-Host "WorkTool C++ app: $OutputDirectory"
