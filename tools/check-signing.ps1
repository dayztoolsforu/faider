# Checks code signing before a release, without building anything: lists the code-signing
# certificates Windows can use, says which one FAIDER_SIGN_CERT picks, then signs and checks a
# throwaway copy of a small program (signtool itself) in your temp folder.
#
#   $env:FAIDER_SIGN_CERT = 'certum'      (Certum's cloud certificate: log in to SimplySign Desktop first)
#   powershell -ExecutionPolicy Bypass -File tools\check-signing.ps1

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'signing.ps1')

$signtool = Find-Signtool
if ($signtool) { Write-Host "signtool: $signtool" }
else { Write-Host 'signtool: NOT FOUND - install the Windows SDK (it comes with Visual Studio Build Tools)' -ForegroundColor Red; exit 1 }

$certs = @(Get-CodeSigningCertificates)
if ($certs.Count -eq 0) {
    Write-Host 'Code-signing certificates: none valid in your certificate store.' -ForegroundColor Yellow
    Write-Host '  Certum cloud certificate: open SimplySign Desktop and log in (code from the SimplySign phone app), then run this again.'
} else {
    Write-Host 'Code-signing certificates:'
    foreach ($c in $certs) {
        "  {0}  {1}  issued by {2}, valid until {3:d MMM yyyy}" -f $c.Thumbprint, $c.GetNameInfo('SimpleName', $false), $c.GetNameInfo('SimpleName', $true), $c.NotAfter
    }
}

if (-not $env:FAIDER_SIGN_CERT) {
    Write-Host ''
    Write-Host 'FAIDER_SIGN_CERT is not set, so builds are not signed. For Certum:' -ForegroundColor Yellow
    Write-Host '  $env:FAIDER_SIGN_CERT = ''certum'''
    Write-Host '  (or for every new terminal: [Environment]::SetEnvironmentVariable(''FAIDER_SIGN_CERT'', ''certum'', ''User''))'
    exit 1
}

try {
    $signing = Get-FaiderSigning
    Write-Host ''
    Write-Host "FAIDER_SIGN_CERT=$($env:FAIDER_SIGN_CERT) signs with $($signing.Description)"
    Write-Host "Timestamp server: $($signing.Timestamp)"

    $trial = Join-Path ([IO.Path]::GetTempPath()) ("faider-sign-check-{0}.exe" -f [guid]::NewGuid().ToString('N').Substring(0, 8))
    Copy-Item $signtool $trial
    try {
        Write-Host "Signing a test copy ($trial)..."
        & $signing.Signtool @($signing.Args) $trial | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "signtool exit $LASTEXITCODE (run it again with /v added to see why)" }
        Assert-FaiderSigned $signing $trial
        $sig = Get-AuthenticodeSignature $trial
        Write-Host ("OK: signed by {0}, timestamped by {1}, trusted by Windows." -f $sig.SignerCertificate.GetNameInfo('SimpleName', $false), $sig.TimeStamperCertificate.GetNameInfo('SimpleName', $false)) -ForegroundColor Green
        Write-Host 'Ready: powershell -ExecutionPolicy Bypass -File tools\build-installer.ps1 -Sign'
    } finally {
        Remove-Item $trial -Force -ErrorAction SilentlyContinue
    }
} catch {
    Write-Host "NOT READY: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
