param(
    [string]$OutputZip = "$PSScriptRoot/artifacts/WorkTool-Portable-Windows-x64.zip",
    [string]$QuickJsSource
)

$ErrorActionPreference = 'Stop'
$zipPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputZip)
if (Test-Path -LiteralPath $zipPath) {
    throw "Output already exists. Specify a different OutputZip: $zipPath"
}

$artifacts = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'artifacts'))
$packageRoot = Join-Path $artifacts ("stage-native-" + [guid]::NewGuid())
$publishDirectory = Join-Path $packageRoot 'App'
$dataDirectory = Join-Path $packageRoot 'Data'
try {
    [IO.Directory]::CreateDirectory($publishDirectory) | Out-Null
    [IO.Directory]::CreateDirectory($dataDirectory) | Out-Null
    & "$PSScriptRoot/build-native.ps1" -OutputDirectory $publishDirectory -QuickJsSource $QuickJsSource

    if (!(Test-Path -LiteralPath (Join-Path $publishDirectory 'WorkTool.WinUI.exe') -PathType Leaf)) {
        throw 'WorkTool.WinUI.exe is missing from the publish output.'
    }

    [IO.File]::WriteAllText((Join-Path $publishDirectory 'portable.mode'), '')
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (!$vsPath) {
        throw 'The Visual Studio x64 C++ build tools are required.'
    }

    & (Join-Path $vsPath 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    $launcherPath = Join-Path $packageRoot 'WorkTool.exe'
    $launcherObject = Join-Path $packageRoot 'WorkToolLauncher.obj'
    $launcherResource = Join-Path $packageRoot 'WorkToolLauncher.res'
    & rc.exe /nologo "/i$PSScriptRoot" "/fo$launcherResource" "$PSScriptRoot/WorkToolLauncher.rc"
    if ($LASTEXITCODE -ne 0) {
        throw 'Portable launcher icon resource build failed.'
    }
    & cl.exe /nologo /utf-8 /O2 /MT "/Fo$launcherObject" "/Fe$launcherPath" "$PSScriptRoot/WorkToolLauncher.c" /link /SUBSYSTEM:WINDOWS user32.lib $launcherResource
    if ($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $launcherPath -PathType Leaf)) {
        throw 'Portable launcher build failed.'
    }
    Remove-Item -LiteralPath $launcherObject, $launcherResource

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
    if ((Split-Path ([IO.Path]::GetFullPath($packageRoot))) -eq $artifacts -and (Test-Path -LiteralPath $packageRoot)) {
        Remove-Item -LiteralPath $packageRoot -Recurse -Force
    }
}
