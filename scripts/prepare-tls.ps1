param([string]$Proxy = $env:HTTPS_PROXY)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$deps = Join-Path $root '.deps'
$archive = Join-Path $deps 'MinGit-2.56.0.3-64-bit.zip'
$target = Join-Path $deps 'openssl-mingit-2.56.0.3'
$expected = '2E5619966E588FDE5F8122C5557CAD50C0FBAFCAED736689F40EC1DFB1D78EA7'
$uri = 'https://github.com/git-for-windows/git/releases/download/v2.56.0.windows.3/MinGit-2.56.0.3-64-bit.zip'
New-Item -ItemType Directory -Path $deps -Force | Out-Null
if (!(Test-Path -LiteralPath $archive) -or (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expected) {
    $options = @{ Uri=$uri; OutFile=$archive }
    if ($Proxy) { $options.Proxy = $Proxy }
    try { Invoke-WebRequest @options }
    catch {
        $python = Get-Command python -ErrorAction SilentlyContinue
        if (!$python) { throw }
        # Standard-library HTTPS verifies certificates; inputs remain separate arguments.
        $fetch = 'import pathlib,sys,urllib.request; proxy=urllib.request.ProxyHandler({"https":sys.argv[3]} if sys.argv[3] else {}); data=urllib.request.build_opener(proxy).open(sys.argv[1],timeout=90).read(); pathlib.Path(sys.argv[2]).write_bytes(data)'
        & $python.Source -c $fetch $uri $archive $Proxy
        if ($LASTEXITCODE -ne 0) { throw 'OpenSSL download failed' }
    }
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expected) { throw 'OpenSSL archive checksum mismatch' }
New-Item -ItemType Directory -Path $target -Force | Out-Null
$zip = [IO.Compression.ZipFile]::OpenRead($archive)
try {
    $files = @{
        'ucrt64/bin/libssl-3-x64.dll' = 'libssl-3-x64.dll'
        'ucrt64/bin/libcrypto-3-x64.dll' = 'libcrypto-3-x64.dll'
        'ucrt64/share/licenses/openssl/LICENSE' = 'OPENSSL-LICENSE.txt'
    }
    foreach ($name in $files.Keys) {
        $entry = $zip.GetEntry($name)
        if (!$entry) { throw "Missing OpenSSL archive entry: $name" }
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, (Join-Path $target $files[$name]), $true)
    }
} finally { $zip.Dispose() }
Write-Output $target
