# WorkTool (C++/WinUI 3) 빌드. 결과: build\<구성>\WorkTool.exe (폴더 전체가 배포 단위예요)
#   ./build.ps1            릴리스 빌드
#   ./build.ps1 -Test      빌드 후 가상 자료로 화면 검증
param([string]$Configuration = 'Release', [switch]$Test)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$msbuild = & $vswhere -latest -prerelease -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) { throw 'Visual Studio(C++ 데스크톱 개발)의 MSBuild를 찾지 못했어요.' }
& $msbuild WorkTool.vcxproj -restore -p:Configuration=$Configuration -p:Platform=x64 -m -v:m -nologo
if ($LASTEXITCODE -ne 0) { throw '빌드에 실패했어요.' }
if ($Test) {
    $out = Join-Path $PSScriptRoot 'build\smoke'
    Remove-Item -Recurse -Force $out -ErrorAction SilentlyContinue
    $app = Start-Process "build\$Configuration\WorkTool.exe" -ArgumentList '--smoke-test', "`"$out`"", "`"$PSScriptRoot\tests\fixtures`"" -PassThru
    $until = (Get-Date).AddMinutes(3)
    while ((Get-Date) -lt $until -and -not $app.HasExited -and -not (Test-Path "$out\smoke-result.json")) { Start-Sleep -Milliseconds 500 }
    Start-Sleep 1
    if (-not $app.HasExited) { $app.Kill() }
    if (-not (Test-Path "$out\smoke-result.json")) { throw '화면 검증 결과가 만들어지지 않았어요.' }
    $result = Get-Content "$out\smoke-result.json" -Encoding utf8 -Raw | ConvertFrom-Json
    if (-not $result.passed) { throw "화면 검증 실패: $($result.error)" }
    "화면 검증 $($result.checks.Count)개 통과"
}
