param(
    [string]$OutputZip = "$PSScriptRoot/artifacts/WorkTool-Portable-Windows-x64.zip"
)

$ErrorActionPreference = 'Stop'
$zipPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputZip)
if (Test-Path -LiteralPath $zipPath) {
    throw "Output already exists. Specify a different OutputZip: $zipPath"
}

$packageRoot = Join-Path ([IO.Path]::GetTempPath()) ("WorkTool-Portable-" + [guid]::NewGuid())
$publishDirectory = Join-Path $packageRoot 'App'
$dataDirectory = Join-Path $packageRoot 'Data'
try {
    [IO.Directory]::CreateDirectory($publishDirectory) | Out-Null
    [IO.Directory]::CreateDirectory($dataDirectory) | Out-Null
    & "$PSScriptRoot/build.ps1" -OutputDirectory $publishDirectory

    if (!(Test-Path -LiteralPath (Join-Path $publishDirectory 'WorkTool.WinUI.exe') -PathType Leaf)) {
        throw 'WorkTool.WinUI.exe is missing from the publish output.'
    }

    [IO.File]::WriteAllText((Join-Path $publishDirectory 'portable.mode'), '')
    $launcher = @'
@echo off
setlocal
if not exist "%~dp0Data\." mkdir "%~dp0Data"
if not exist "%~dp0Data\." goto dataError
if not exist "%~dp0App\WorkTool.WinUI.exe" goto appError
start "" /D "%~dp0App" "%~dp0App\WorkTool.WinUI.exe"
exit /b %errorlevel%

:dataError
echo Could not create the Data folder. Extract the ZIP into a writable folder.
pause
exit /b 1

:appError
echo WorkTool.WinUI.exe is missing. Extract the full ZIP.
pause
exit /b 1
'@
    [IO.File]::WriteAllText((Join-Path $packageRoot 'WorkTool.cmd'), ($launcher -replace '\r?\n', "`r`n") + "`r`n", [Text.Encoding]::ASCII)

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($zipPath)) | Out-Null
    $stagedZip = Join-Path ([IO.Path]::GetDirectoryName($zipPath)) ('.WorkTool-' + [guid]::NewGuid() + '.tmp')
    try {
        [IO.Compression.ZipFile]::CreateFromDirectory($packageRoot, $stagedZip)
        [IO.File]::Move($stagedZip, $zipPath)
    }
    finally {
        [IO.File]::Delete($stagedZip)
    }
    Write-Host "Portable ZIP: $zipPath"
}
finally {
    Remove-Item -LiteralPath $packageRoot -Recurse -Force
}
