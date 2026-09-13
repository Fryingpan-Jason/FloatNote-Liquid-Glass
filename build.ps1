[CmdletBinding()]
param(
    [string]$OutputDirectory = 'build',
    [ValidateSet('x64', 'x86', 'arm64')]
    [string]$Architecture = 'x64',
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',
    [switch]$Test,
    [string]$SourceFile = '',
    [switch]$NoRun
)

$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$outputPath = Join-Path $projectRoot $OutputDirectory
$sourcePath = Join-Path $projectRoot 'experiments\local_desktop.cpp'
if ($Test) { $sourcePath = Join-Path $projectRoot 'tests\regression.cpp' }
if ($SourceFile) { $sourcePath = Join-Path $projectRoot $SourceFile }

$sourcePath = [IO.Path]::GetFullPath($sourcePath)
if (-not $sourcePath.StartsWith([IO.Path]::GetFullPath($projectRoot) + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'SourceFile must stay inside the FloatNote project.'
}
if (-not (Test-Path -LiteralPath $sourcePath)) { throw "Source file not found: $sourcePath" }

$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer (vswhere.exe) was not found.' }

$toolComponent = if ($Architecture -eq 'arm64') {
    'Microsoft.VisualStudio.Component.VC.Tools.ARM64'
} else {
    'Microsoft.VisualStudio.Component.VC.Tools.x86.x64'
}
$visualStudioPath = & $vswhere -latest -products * -requires $toolComponent -property installationPath
if (-not $visualStudioPath) { throw "Visual Studio C++ tools for $Architecture were not found." }

$developerShell = Join-Path $visualStudioPath 'Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path -LiteralPath $developerShell)) { throw "Visual Studio developer shell not found: $developerShell" }

New-Item -ItemType Directory -Force -Path $outputPath | Out-Null

$baseName = if ($Test) { [IO.Path]::GetFileNameWithoutExtension($sourcePath) } else { 'FloatNote' }
$objectPath = Join-Path $outputPath "$baseName.obj"
$resourcePath = Join-Path $outputPath 'FloatNote.res'
$artifactPath = Join-Path $outputPath "$baseName.exe"
$manifestPath = Join-Path $projectRoot 'src\FloatNote.manifest'
$resourceSource = Join-Path $projectRoot 'src\resources.rc'

$compilerFlags = '/O2 /MT'
if ($Configuration -eq 'Debug') { $compilerFlags = '/Od /MTd /Zi /D_DEBUG' }
$subsystem = if ($Test) { 'CONSOLE' } else { 'WINDOWS' }
$hostArchitecture = if ([Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture -eq 'Arm64') { 'arm64' } else { 'x64' }

$compileCommand = @(
    ('call "{0}" -arch={1} -host_arch={2} >nul' -f $developerShell, $Architecture, $hostArchitecture),
    ('rc.exe /nologo /fo"{0}" "{1}"' -f $resourcePath, $resourceSource),
    ('cl.exe /nologo /std:c++20 /utf-8 /EHsc /W4 /permissive- /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 {0} /Fo:"{1}" /Fe:"{2}" "{3}" "{4}" /link /SUBSYSTEM:{5} /MANIFEST:EMBED /MANIFESTINPUT:"{6}" user32.lib gdi32.lib shell32.lib ole32.lib comctl32.lib comdlg32.lib dwmapi.lib shlwapi.lib advapi32.lib uxtheme.lib gdiplus.lib' -f
        $compilerFlags, $objectPath, $artifactPath, $sourcePath, $resourcePath, $subsystem, $manifestPath)
) -join ' && '

& $env:ComSpec /d /s /c $compileCommand
if ($LASTEXITCODE -ne 0) { throw "FloatNote build failed with compiler exit code $LASTEXITCODE." }

$artifact = Get-Item -LiteralPath $artifactPath
$noticesPath = Join-Path $projectRoot 'THIRD_PARTY_NOTICES.md'
if (Test-Path -LiteralPath $noticesPath) {
    Copy-Item -LiteralPath $noticesPath -Destination (Join-Path $outputPath 'THIRD_PARTY_NOTICES.md') -Force
}
Write-Host ("Built {0} {1}: {2} ({3:N0} bytes)" -f $Configuration, $Architecture, $artifact.FullName, $artifact.Length)

if ($Test -and -not $NoRun) {
    if ($Architecture -eq 'arm64' -and $env:PROCESSOR_ARCHITECTURE -ne 'ARM64') {
        Write-Host 'ARM64 test binary built but not run on this host.'
    } else {
        & $artifact.FullName
        if ($LASTEXITCODE -ne 0) { throw "FloatNote test failed: $baseName (exit code $LASTEXITCODE)" }
    }
}

