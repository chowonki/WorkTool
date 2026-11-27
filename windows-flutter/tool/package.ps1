# 포터블 배포본(ZIP)을 만들어요. `flutter build windows --release` 뒤에 실행해요.
#
#   WorkTool\
#     WorkTool.exe   시작 프로그램 (windows/launcher)
#     App\           앱 파일: WorkTool.exe, DLL, data\ (Flutter 엔진·규칙)
#     Data\          설정·오늘 기록 (처음엔 비어 있어요)
#
# 사용법: pwsh windows-flutter/tool/package.ps1 [-ZipName WorkTool-Windows-x64.zip] [-OutDir <폴더>]
# Windows PowerShell 5.1과 PowerShell 7 모두에서 돌아요.
param(
  [string]$ZipName = "WorkTool-Windows-x64.zip",
  [string]$OutDir = ""
)
$ErrorActionPreference = "Stop"

$project = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $OutDir) { $OutDir = Join-Path $project "artifacts" }
$build = Join-Path $project "build\windows\x64"
$appSrc = Join-Path $build "runner\Release"
$launcher = Join-Path $build "launcher\Release\WorkTool.exe"
foreach ($need in $appSrc, $launcher) {
  if (-not (Test-Path $need)) { throw "$need 가 없어요. 먼저 flutter build windows --release 를 실행해 주세요." }
}

$root = Join-Path $OutDir "WorkTool"
$zip = Join-Path $OutDir $ZipName
if (Test-Path $root) { Remove-Item $root -Recurse -Force }
if (Test-Path $zip) { Remove-Item $zip -Force }
$app = Join-Path $root "App"
New-Item -ItemType Directory -Force $app, (Join-Path $root "Data") | Out-Null

Copy-Item $launcher (Join-Path $root "WorkTool.exe")
Copy-Item (Join-Path $appSrc "*") $app -Recurse
Copy-Item (Join-Path $project "assets\fonts\Pretendard-LICENSE.txt") $app
# App 폴더에 이 파일이 있으면 설정을 사용자 폴더가 아니라 옆의 Data 폴더에 저장해요.
New-Item -ItemType File -Force (Join-Path $app "portable.mode") | Out-Null

# 빠지면 실행이 안 되는 파일들이에요. 꼭 있는지 확인해요.
$required = "WorkTool.exe", "App\WorkTool.exe", "App\flutter_windows.dll", "App\worktool_js.dll",
  "App\msvcp140.dll", "App\vcruntime140.dll", "App\vcruntime140_1.dll",
  "App\data\app.so", "App\data\icudtl.dat", "App\data\flutter_assets\assets\js\excel.js", "App\portable.mode"
foreach ($f in $required) {
  if (-not (Test-Path (Join-Path $root $f))) { throw "포터블 폴더에 $f 가 없어요" }
}
# 맨 위에는 시작 프로그램과 App·Data 폴더만 있어야 해요.
$top = (Get-ChildItem $root | Sort-Object Name | ForEach-Object { $_.Name }) -join ","
if ($top -ne "App,Data,WorkTool.exe") { throw "맨 위 구성이 달라요: $top" }

# ZIP: 경로 구분자는 / 로, 빈 Data 폴더도 항목으로 넣어요 (Compress-Archive는 빈 폴더를 빼요).
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$base = (Split-Path $root -Parent).TrimEnd('\') + '\'
$archive = [System.IO.Compression.ZipFile]::Open($zip, [System.IO.Compression.ZipArchiveMode]::Create)
try {
  foreach ($item in Get-ChildItem $root -Recurse | Sort-Object FullName) {
    $name = $item.FullName.Substring($base.Length).Replace('\', '/')
    if ($item.PSIsContainer) {
      if (-not (Get-ChildItem $item.FullName -Force | Select-Object -First 1)) { [void]$archive.CreateEntry("$name/") }
    } else {
      [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
        $archive, $item.FullName, $name, [System.IO.Compression.CompressionLevel]::Optimal)
    }
  }
} finally {
  $archive.Dispose()
}

$size = (Get-Item $zip).Length
Write-Host ("{0}  {1:N0} bytes ({2:N1} MB)" -f $zip, $size, ($size / 1MB))
Write-Host ("SHA256 " + (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower())
