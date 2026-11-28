# 포터블 ZIP 만들기: WorkTool\WorkTool.exe(실행기) + App(프로그램) + Data(설정·오늘 기록)
#   ./build-portable.ps1            build\WorkTool-Windows-x64-cpp.zip
param([string]$OutputZip = "$PSScriptRoot/build/WorkTool-Windows-x64-cpp.zip")
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
& "$PSScriptRoot/build.ps1"
$stage = Join-Path $PSScriptRoot 'build\portable'
Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
$root = Join-Path $stage 'WorkTool'
$app = Join-Path $root 'App'
New-Item -ItemType Directory -Force $app, (Join-Path $root 'Data') | Out-Null
# 실행에 필요 없는 디버그 기호·형식 정보·가져오기 라이브러리는 빼요.
robocopy "$PSScriptRoot\build\Release" $app /E /XF *.pdb *.winmd *.lib *.exp *.ilk /NFL /NDL /NJH /NJS | Out-Null
if ($LASTEXITCODE -ge 8) { throw '파일 복사에 실패했어요.' }
if (-not (Test-Path "$app\WorkTool.exe")) { throw 'WorkTool.exe가 없어요.' }
[IO.File]::WriteAllText((Join-Path $app 'portable.mode'), '')
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -prerelease -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio x64 C++ 도구가 필요해요.' }
& "$vs\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
Set-Location $PSScriptRoot
& rc.exe /nologo "/fo$stage\Launcher.res" Launcher.rc
if ($LASTEXITCODE -ne 0) { throw '실행기 아이콘을 만들지 못했어요.' }
& cl.exe /nologo /utf-8 /O2 /MT "/Fo$stage\Launcher.obj" "/Fe$root\WorkTool.exe" Launcher.c /link /SUBSYSTEM:WINDOWS user32.lib "$stage\Launcher.res"
if ($LASTEXITCODE -ne 0) { throw '실행기를 만들지 못했어요.' }
Remove-Item "$stage\Launcher.obj", "$stage\Launcher.res"
$zip = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputZip)
Remove-Item $zip -ErrorAction SilentlyContinue
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($stage, $zip, [IO.Compression.CompressionLevel]::Optimal, $false)
"포터블 ZIP: $zip ({0:N1} MB)" -f ((Get-Item $zip).Length / 1MB)
