# Code signing for Faider's builds, shared by build-installer.ps1 and check-signing.ps1 (dot-source it).
#
# FAIDER_SIGN_CERT says which certificate signs:
#   certum         the newest valid code-signing certificate from Certum in your certificate store:
#                  with Certum's cloud certificate, SimplySign Desktop puts it there while you're
#                  logged in, so this keeps working when the certificate is renewed
#   <thumbprint>   that certificate in your store (CurrentUser\My, then LocalMachine\My)
#   <path>.pfx     a certificate file, with FAIDER_SIGN_PASSWORD as its password
# FAIDER_SIGN_TIMESTAMP overrides the timestamp server (else Certum's for a Certum certificate,
# DigiCert's otherwise). A timestamp keeps a signature valid after the certificate expires.

$script:certumTimestamp = 'http://time.certum.pl'
$script:otherTimestamp = 'http://timestamp.digicert.com'
$script:codeSigningUsage = '1.3.6.1.5.5.7.3.3'

function Find-Signtool {
    Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" -ErrorAction SilentlyContinue |
        Sort-Object { $v = $null; [void][version]::TryParse($_.Directory.Parent.Name, [ref]$v); $v } -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}

# Code-signing certificates with their private key, valid today, newest first.
function Get-CodeSigningCertificates {
    $now = Get-Date
    $found = foreach ($store in 'Cert:\CurrentUser\My', 'Cert:\LocalMachine\My') {
        Get-ChildItem $store -ErrorAction SilentlyContinue | Where-Object {
            $_.HasPrivateKey -and $_.NotBefore -le $now -and $_.NotAfter -gt $now -and
            ($_.EnhancedKeyUsageList.ObjectId -contains $script:codeSigningUsage)
        }
    }
    $found | Sort-Object NotAfter -Descending
}

function Test-CertumIssued ($cert) { $cert.Issuer -match 'Certum' }

# What signs, as an object: Signtool, Args (signtool's arguments before the file), Description,
# Certificate (when it's from the store). Throws, saying why, when FAIDER_SIGN_CERT can't be used.
# $null when FAIDER_SIGN_CERT isn't set.
function Get-FaiderSigning {
    $choice = $env:FAIDER_SIGN_CERT
    if (-not $choice) { return $null }

    $signtool = Find-Signtool
    if (-not $signtool) { throw 'signtool.exe was not found: install the Windows SDK (it comes with Visual Studio Build Tools)' }

    $cert = $null
    $certumSigned = $false
    if ($choice -eq 'certum') {
        $cert = Get-CodeSigningCertificates | Where-Object { Test-CertumIssued $_ } | Select-Object -First 1
        if (-not $cert) {
            throw 'No valid Certum code-signing certificate in your certificate store. With the cloud certificate: open SimplySign Desktop and log in (it needs the code from the SimplySign app on your phone), then try again.'
        }
        $certumSigned = $true
    } elseif (Test-Path $choice -PathType Leaf) {
        $pfx = $choice
    } else {
        $thumb = ($choice -replace '\s', '').ToUpperInvariant()
        $cert = Get-CodeSigningCertificates | Where-Object { $_.Thumbprint -eq $thumb } | Select-Object -First 1
        if (-not $cert) { throw "No valid code-signing certificate with thumbprint $thumb in your certificate store (for SimplySign: are you logged in?)" }
        $certumSigned = Test-CertumIssued $cert
    }

    $timestamp = if ($env:FAIDER_SIGN_TIMESTAMP) { $env:FAIDER_SIGN_TIMESTAMP } elseif ($certumSigned) { $script:certumTimestamp } else { $script:otherTimestamp }
    $signArgs = @('sign', '/fd', 'sha256', '/td', 'sha256', '/tr', $timestamp)
    if ($cert) {
        $signArgs += '/sha1', $cert.Thumbprint
        $description = "{0} (issued by {1}, valid until {2:d MMM yyyy})" -f $cert.GetNameInfo('SimpleName', $false), $cert.GetNameInfo('SimpleName', $true), $cert.NotAfter
    } else {
        $signArgs += '/f', $pfx
        if ($env:FAIDER_SIGN_PASSWORD) { $signArgs += '/p', $env:FAIDER_SIGN_PASSWORD }
        $description = "the certificate in $pfx"
    }
    [pscustomobject]@{ Signtool = $signtool; Args = $signArgs; Description = $description; Certificate = $cert; Timestamp = $timestamp }
}

# Signs one file, then checks the signature. Throws if either fails.
function Invoke-FaiderSign ($signing, [string]$file) {
    & $signing.Signtool @($signing.Args) $file
    if ($LASTEXITCODE -ne 0) { throw "Signing $file failed (signtool exit $LASTEXITCODE)" }
    Assert-FaiderSigned $signing $file
}

# Checks a file's signature and timestamp. Throws if it isn't signed by the certificate in use.
function Assert-FaiderSigned ($signing, [string]$file) {
    $sig = Get-AuthenticodeSignature $file
    if ($sig.Status -eq 'NotSigned' -or -not $sig.SignerCertificate) { throw "$file is not signed" }
    if ($signing.Certificate -and $sig.SignerCertificate.Thumbprint -ne $signing.Certificate.Thumbprint) {
        throw "$file is signed by $($sig.SignerCertificate.Subject), not the certificate in use"
    }
    if (-not $sig.TimeStamperCertificate) { throw "$file has no timestamp (the signature would stop being valid when the certificate expires)" }
    if ($sig.Status -ne 'Valid') { throw "$file's signature isn't trusted by Windows: $($sig.StatusMessage)" }
}
