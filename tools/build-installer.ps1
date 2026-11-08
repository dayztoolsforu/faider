# Builds Faider (Release) and its installer: build\installer\Faider-<version>-Setup.exe.
# Needs Inno Setup 6 (winget install JRSoftware.InnoSetup) and the ASIO SDK in third_party (as for
# any build that's handed out: without it the app can't open ASIO interfaces).
#
#   powershell -ExecutionPolicy Bypass -File tools\build-installer.ps1          (signed if FAIDER_SIGN_CERT is set)
#   powershell -ExecutionPolicy Bypass -File tools\build-installer.ps1 -Sign    (a release: stops unless it signs)
#
# Signing: set FAIDER_SIGN_CERT (see tools\signing.ps1; with Certum's cloud certificate it's `certum`,
# after logging in to SimplySign Desktop). Faider.exe is signed before it's packed, Setup.exe and the
# uninstaller inside it by Inno Setup, and each signature is checked. tools\check-signing.ps1 tries it
# first without building anything.

param([switch]$Sign)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'signing.ps1')
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$source = Join-Path $build 'console\Faider_artefacts\Release'
$icon = Join-Path $build 'console\Faider_artefacts\JuceLibraryCode\icon.ico'

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) { $cmake = 'C:\Program Files\CMake\bin\cmake.exe' }
$iscc = @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe", "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe") |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw 'Inno Setup 6 not found (winget install JRSoftware.InnoSetup)' }

# Signing: worked out before the build, so a missing certificate stops it before anything is made
$signing = Get-FaiderSigning
if ($Sign -and -not $signing) { throw '-Sign: set FAIDER_SIGN_CERT first (for Certum: certum, logged in to SimplySign Desktop)' }
if ($signing) { Write-Host "Signing with $($signing.Description), timestamp $($signing.Timestamp)" }

if (-not (Select-String -Path (Join-Path $build 'CMakeCache.txt') -Pattern '^ASIO_SDK_DIR:.*=.+' -Quiet)) {
    throw 'build\CMakeCache.txt has no ASIO_SDK_DIR: the app would be built without ASIO'
}

# The version is the project's (CMakeLists.txt: project(... VERSION x.y.z ...))
$version = (Select-String -Path (Join-Path $root 'CMakeLists.txt') -Pattern 'project\s*\(\s*\w+\s+VERSION\s+([\d.]+)').Matches[0].Groups[1].Value

Write-Host 'Building...'

# The version is in the resource file JUCE writes (the one Windows shows in the file properties),
# and JUCE's rule for that file doesn't watch the version: it is handed the info file as an
# argument but doesn't depend on it, so a new version leaves the old one in the resource. The
# file is put back here so it is written again with the version of this build.
$resource = Join-Path $build 'console\Faider_artefacts\JuceLibraryCode\Faider_resources.rc'
if (Test-Path $resource) { Remove-Item $resource -Force }

$ErrorActionPreference = 'Continue'   # (CMake's warnings on stderr aren't failures: the exit code says)
& $cmake --build $build --config Release --target Faider 2>&1 | ForEach-Object { "$_" }
if ($LASTEXITCODE -ne 0) { throw 'The build failed.' }
$ErrorActionPreference = 'Stop'

# The exe carries the version in its file properties: it has to be this one
$fileVersion = (Get-Item (Join-Path $source 'Faider.exe')).VersionInfo.FileVersion
if ($fileVersion -ne $version) {
    throw "Faider.exe says version $fileVersion, but this build is ${version}: not packed"
}

# Nothing from the build machine's user folder (paths baked in at compile time) goes out in the exe
$bytes = [IO.File]::ReadAllBytes((Join-Path $source 'Faider.exe'))
foreach ($text in [Text.Encoding]::ASCII.GetString($bytes), [Text.Encoding]::Unicode.GetString($bytes)) {
    if ($text.IndexOf($env:USERPROFILE, [StringComparison]::OrdinalIgnoreCase) -ge 0 -or
        $text.IndexOf($env:USERPROFILE.Replace('\', '/'), [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw 'Faider.exe contains a path in the user folder: not packed'
    }
}

# The C++ runtime DLLs of the toolset that built the exe (Build Tools' VC\Redist\MSVC\<version>\x64\
# Microsoft.VC*.CRT), the newest there: installed next to the exe, so no runtime installer has to run
# (it can wait unseen for another Windows install to finish).
$linker = (Select-String -Path (Join-Path $build 'CMakeCache.txt') -Pattern '^CMAKE_LINKER:FILEPATH=(.+)$').Matches[0].Groups[1].Value -replace '/', '\'
$vcRoot = $linker -replace '\\Tools\\MSVC\\.*$', ''
$crt = Get-ChildItem (Join-Path $vcRoot 'Redist\MSVC\*\x64\Microsoft.VC*.CRT') -Directory |
    Where-Object { Test-Path (Join-Path $_.FullName 'vcruntime140_1.dll') } |
    Sort-Object { [version](Get-Item (Join-Path $_.FullName 'vcruntime140.dll')).VersionInfo.FileVersion } -Descending | Select-Object -First 1
if (-not $crt) { throw "No x64 C++ runtime folder under $vcRoot\Redist\MSVC" }
foreach ($dll in 'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll') {
    if (-not (Test-Path (Join-Path $crt.FullName $dll))) { throw "$dll is missing from $($crt.FullName)" }
}
$vc = (Get-Item (Join-Path $crt.FullName 'vcruntime140.dll')).VersionInfo.FileVersion

# Inno Setup signs Setup.exe and the uninstaller with the same command (SignTool in Faider.iss):
# each argument quoted ($q), a $ in one doubled, and $f for the file.
$innoArgs = @()
if ($signing) {
    Write-Host 'Signing Faider.exe...'
    Invoke-FaiderSign $signing (Join-Path $source 'Faider.exe')
    $command = ((@($signing.Signtool) + $signing.Args) | ForEach-Object { '$q' + ($_ -replace '\$', '$$$$') + '$q' }) -join ' '
    $innoArgs = @('/DSign', "/Sfaidersign=$command `$f")
} else {
    Write-Host 'not signed (set FAIDER_SIGN_CERT to sign; tools\check-signing.ps1 checks it)'
}

Write-Host "Packing $version (C++ runtime $vc)..."
& $iscc /Q "/DAppVersion=$version" "/DSource=$source" "/DIcon=$icon" "/DCRT=$($crt.FullName)" @innoArgs (Join-Path $root 'installer\Faider.iss')
if ($LASTEXITCODE -ne 0) { throw 'Inno Setup failed.' }
$setup = Join-Path $build "installer\Faider-$version-Setup.exe"
if ($signing) {
    Assert-FaiderSigned $signing $setup   # (the uninstaller inside it is signed by the same command)
    Write-Host "Signed and timestamped: Faider.exe, Setup.exe and its uninstaller"
}
Get-Item $setup | ForEach-Object { "{0}  {1:N1} MB" -f $_.FullName, ($_.Length / 1MB) }
