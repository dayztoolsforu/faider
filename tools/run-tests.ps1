# Builds and runs Faider's checks (tests\*.cpp): each prints PASS/FAIL per check and exits
# non-zero on a failure. The ones that need hardware or change Windows (DefaultDeviceTest,
# AppRoutingTest, the WASAPI probes) and the plugin-loading one (PluginScanTest) aren't run here.
#
#   powershell -ExecutionPolicy Bypass -File tools\run-tests.ps1
#
# AppCaptureTest needs an audio output device to capture from, so a machine without one
# can't run it. Name the checks to run to leave it out:
#
#   powershell -ExecutionPolicy Bypass -File tools\run-tests.ps1 -Tests MeterTest,PadsTest

param([string[]]$Tests)

$root = Split-Path -Parent $PSScriptRoot
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) { $cmake = 'C:\Program Files\CMake\bin\cmake.exe' }

$tests = $Tests
if ($tests) {
    $tests = @($tests -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
}
if (-not $tests) {
    $tests = 'MeterTest', 'PadsTest', 'TapeTest', 'NoiseReducerTest', 'VoiceChainTest', 'TriggerTest', 'RecorderTest', 'BuffersTest', 'ThemeTest', 'LookRenderTest', 'SessionTest', 'AppCaptureTest'
}

Write-Host 'Building the checks...'
# The build's words are kept for when it fails: a build machine needs to say what went wrong.
$buildOutput = & $cmake --build (Join-Path $root 'build') --config Release --target $tests 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) {
    Write-Host $buildOutput -ForegroundColor Red
    Write-Host 'The build failed.' -ForegroundColor Red
    exit 1
}

$failed = @()
foreach ($t in $tests) {
    $exe = Join-Path $root "build\${t}_artefacts\Release\$t.exe"
    if (-not (Test-Path $exe)) {   # not built (a target renamed or dropped): that's a failure, not a pass
        Write-Host ("FAIL  {0}  (no {1})" -f $t, $exe) -ForegroundColor Red
        $failed += $t
        continue
    }
    $global:LASTEXITCODE = 0   # this check's own exit code, not one left from before
    $out = & $exe 2>&1 | Out-String
    if ($LASTEXITCODE -eq 0) {
        Write-Host ("PASS  {0}" -f $t) -ForegroundColor Green
    } else {
        Write-Host ("FAIL  {0}" -f $t) -ForegroundColor Red
        $out -split "`n" | Where-Object { $_ -match '^FAIL' } | ForEach-Object { Write-Host "      $_" }
        $failed += $t
    }
}
if ($failed.Count -gt 0) { exit 1 }
Write-Host 'All checks passed.'
