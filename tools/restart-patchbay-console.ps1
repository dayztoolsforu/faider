# Rebuilds Patchbay Console from the latest code, installs it and starts it again (run by "Restart
# Patchbay Console.cmd"). It runs from C:\PatchbayConsole,
# keeps running in the tray when its window is closed (so it's quit properly first), and a failed
# build starts the installed copy again. A desktop shortcut is made the first time.

$ErrorActionPreference = 'Stop'
trap {
    Write-Host $_ -ForegroundColor Red
    if ($exe -and (Test-Path $exe) -and -not (Get-Process 'Patchbay Console' -ErrorAction SilentlyContinue)) { Start-Process $exe }
    Read-Host 'Press Enter to close'
    exit 1
}
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build\console\PatchbayConsole_artefacts\Release'
$install = 'C:\PatchbayConsole'
$exe = Join-Path $install 'Patchbay Console.exe'

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) { $cmake = 'C:\Program Files\CMake\bin\cmake.exe' }

$running = Get-Process 'Patchbay Console' -ErrorAction SilentlyContinue | Select-Object -First 1
if ($running) {
    Write-Host 'Quitting Patchbay Console...'
    $quitter = if ($running.Path) { $running.Path } elseif (Test-Path $exe) { $exe } else { Join-Path $build 'Patchbay Console.exe' }
    & $quitter --quit
    for ($i = 0; $i -lt 40 -and (Get-Process 'Patchbay Console' -ErrorAction SilentlyContinue); $i++) { Start-Sleep -Milliseconds 250 }
    if (Get-Process 'Patchbay Console' -ErrorAction SilentlyContinue) {
        Write-Host 'Patchbay Console did not quit. Close it from the tray icon and run this again.' -ForegroundColor Red
        Read-Host 'Press Enter to close'
        exit 1
    }
}

Write-Host 'Building...'
$ErrorActionPreference = 'Continue'   # (CMake's warnings on stderr aren't failures: the exit code says)
& $cmake --build (Join-Path $root 'build') --config Release --target PatchbayConsole 2>&1 | ForEach-Object { "$_" }
$built = $LASTEXITCODE -eq 0
$ErrorActionPreference = 'Stop'

if ($built) {
    Write-Host "Installing to $install..."
    New-Item -ItemType Directory -Force $install | Out-Null
    $locked = @()
    foreach ($file in Get-ChildItem $build -Recurse -File) {
        $to = Join-Path $install $file.FullName.Substring($build.Length).TrimStart('\')
        try {
            New-Item -ItemType Directory -Force (Split-Path -Parent $to) | Out-Null
            Copy-Item $file.FullName $to -Force
        } catch { $locked += $to }
    }
    if ($locked.Count -gt 0) {
        Write-Host "Couldn't replace (in use?), the old copy is still there:" -ForegroundColor Yellow
        $locked | ForEach-Object { Write-Host "  $_" -ForegroundColor Yellow }
    }
    $link = Join-Path ([Environment]::GetFolderPath('Desktop')) 'Patchbay Console.lnk'
    if (-not (Test-Path $link)) {
        $shortcut = (New-Object -ComObject WScript.Shell).CreateShortcut($link)
        $shortcut.TargetPath = $exe
        $shortcut.WorkingDirectory = $install
        $shortcut.Save()
        Write-Host "Made a desktop shortcut: $link"
    }
} else {
    Write-Host ''
    Write-Host 'The build failed (errors above). Starting the installed copy (the previous build).' -ForegroundColor Red
}

if (Test-Path $exe) {
    Write-Host 'Starting Patchbay Console...'
    Start-Process $exe
}

if (-not $built) {
    Read-Host 'Press Enter to close'
    exit 1
}
Start-Sleep -Seconds 2
