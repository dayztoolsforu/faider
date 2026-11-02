# Builds Patchbay Console (Release) and its installer: build\installer\PatchbayConsole-<version>-Setup.exe.
# Needs Inno Setup 6 (winget install JRSoftware.InnoSetup) and the ASIO SDK in third_party (as for
# any build that's handed out: without it the app can't open ASIO interfaces).
#
#   powershell -ExecutionPolicy Bypass -File tools\build-installer.ps1

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$source = Join-Path $build 'console\PatchbayConsole_artefacts\Release'
$icon = Join-Path $build 'console\PatchbayConsole_artefacts\JuceLibraryCode\icon.ico'

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) { $cmake = 'C:\Program Files\CMake\bin\cmake.exe' }
$iscc = @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe") |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw 'Inno Setup 6 not found (winget install JRSoftware.InnoSetup)' }

if (-not (Select-String -Path (Join-Path $build 'CMakeCache.txt') -Pattern '^ASIO_SDK_DIR:.*=.+' -Quiet)) {
    throw 'build\CMakeCache.txt has no ASIO_SDK_DIR: the app would be built without ASIO'
}

# The version is the project's (CMakeLists.txt: project(... VERSION x.y.z ...))
$version = (Select-String -Path (Join-Path $root 'CMakeLists.txt') -Pattern 'project\s*\(\s*\w+\s+VERSION\s+([\d.]+)').Matches[0].Groups[1].Value

Write-Host 'Building...'
$ErrorActionPreference = 'Continue'   # (CMake's warnings on stderr aren't failures: the exit code says)
& $cmake --build $build --config Release --target PatchbayConsole 2>&1 | ForEach-Object { "$_" }
if ($LASTEXITCODE -ne 0) { throw 'The build failed.' }
$ErrorActionPreference = 'Stop'

# Nothing from the build machine's user folder (paths baked in at compile time) goes out in the exe
$bytes = [IO.File]::ReadAllBytes((Join-Path $source 'Patchbay Console.exe'))
foreach ($text in [Text.Encoding]::ASCII.GetString($bytes), [Text.Encoding]::Unicode.GetString($bytes)) {
    if ($text.IndexOf($env:USERPROFILE, [StringComparison]::OrdinalIgnoreCase) -ge 0 -or
        $text.IndexOf($env:USERPROFILE.Replace('\', '/'), [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw 'Patchbay Console.exe contains a path in the user folder: not packed'
    }
}

# The VC++ runtime installer of the toolset that built the exe (Build Tools' VC\Redist\MSVC\<version>)
$linker = (Select-String -Path (Join-Path $build 'CMakeCache.txt') -Pattern '^CMAKE_LINKER:FILEPATH=(.+)$').Matches[0].Groups[1].Value -replace '/', '\'
$vcRoot = $linker -replace '\\Tools\\MSVC\\.*$', ''
$redist = Get-ChildItem (Join-Path $vcRoot 'Redist\MSVC') -Recurse -Filter vc_redist.x64.exe |
    Sort-Object { [version]$_.VersionInfo.FileVersion } -Descending | Select-Object -First 1
if (-not $redist) { throw "No vc_redist.x64.exe under $vcRoot\Redist\MSVC" }
$vc = [version]$redist.VersionInfo.FileVersion

Write-Host "Packing $version (VC++ runtime $vc)..."
& $iscc /Q "/DAppVersion=$version" "/DSource=$source" "/DIcon=$icon" "/DVCRedist=$($redist.FullName)" `
    "/DVCMajor=$($vc.Major)" "/DVCMinor=$($vc.Minor)" "/DVCBld=$($vc.Build)" (Join-Path $root 'installer\PatchbayConsole.iss')
if ($LASTEXITCODE -ne 0) { throw 'Inno Setup failed.' }
Get-Item (Join-Path $build "installer\PatchbayConsole-$version-Setup.exe") | ForEach-Object { "{0}  {1:N1} MB" -f $_.FullName, ($_.Length / 1MB) }
