param([string]$Proxy = $env:HTTPS_PROXY)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$deps = Join-Path $root '.deps'
$archive = Join-Path $deps 'pdfium-win-x64-8111.tgz'
$target = Join-Path $deps 'pdfium-8111'
$expected = '3452D11031050EC65E57F180215B57B9E6D465E3FBEDAFA8A77AD90885C7D23E'
$uri = 'https://github.com/bblanchon/pdfium-binaries/releases/download/chromium/8111/pdfium-win-x64.tgz'
New-Item -ItemType Directory -Path $deps -Force | Out-Null
if (!(Test-Path -LiteralPath $archive) -or (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expected) {
    $options = @{ Uri=$uri; OutFile=$archive }
    if ($Proxy) { $options.Proxy = $Proxy }
    try { Invoke-WebRequest @options }
    catch {
        $python = Get-Command python -ErrorAction SilentlyContinue
        if (!$python) { throw }
        # Standard-library HTTPS keeps certificate verification enabled. Arguments
        # are separate; no file name or proxy text is interpolated into Python code.
        $fetch = 'import pathlib,sys,urllib.request; proxy=urllib.request.ProxyHandler({"https":sys.argv[3]} if sys.argv[3] else {}); data=urllib.request.build_opener(proxy).open(sys.argv[1],timeout=90).read(); pathlib.Path(sys.argv[2]).write_bytes(data)'
        & $python.Source -c $fetch $uri $archive $Proxy
        if ($LASTEXITCODE -ne 0) { throw 'PDFium download failed' }
    }
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expected) { throw 'PDFium checksum mismatch' }
New-Item -ItemType Directory -Path $target -Force | Out-Null
& tar -xzf $archive -C $target
if ($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath (Join-Path $target 'bin/pdfium.dll'))) { throw 'PDFium extraction failed' }
Write-Output $target
