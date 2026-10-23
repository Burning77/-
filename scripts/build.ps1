param([ValidateSet('Debug','Release')][string]$Configuration = 'Debug', [string]$BuildRoot = $env:INTERVIEW_BUILD_ROOT)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$sourceRoot = $root
& (Join-Path $sourceRoot 'scripts/prepare-resume.ps1') | Out-Null
& (Join-Path $sourceRoot 'scripts/prepare-tls.ps1') | Out-Null
if ($root -match '[^\x00-\x7F]') {
    $hash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($root))).Substring(0,12)
    $alias = if ($BuildRoot) { $BuildRoot } else { Join-Path ([IO.Path]::GetTempPath()) "QtInterview-$hash" }
    New-Item -ItemType Directory -Path $alias -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $root 'CMakeLists.txt') -Destination $alias -Force
    foreach ($folder in @('src','tests')) { Copy-Item -LiteralPath (Join-Path $root $folder) -Destination $alias -Recurse -Force }
    $root = $alias
}
$qtRoot = 'D:/Qt/6.11.2/mingw_64'
$env:PATH = 'D:\Qt\Tools\mingw1310_64\bin;D:\Qt\Tools\Ninja;D:\Qt\6.11.2\mingw_64\bin;' + $env:PATH
$cmake = 'D:/Qt/Tools/CMake_64/bin/cmake.exe'
$build = Join-Path $root $(if ($Configuration -eq 'Release') { 'build-release' } else { 'build-native' })
& $cmake --fresh -S $root -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration" '-DCMAKE_CXX_COMPILER=D:/Qt/Tools/mingw1310_64/bin/g++.exe' '-DCMAKE_MAKE_PROGRAM=D:/Qt/Tools/Ninja/ninja.exe' "-DCMAKE_PREFIX_PATH=$qtRoot"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $cmake --build $build --parallel 4
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$tls = Join-Path $build 'tls'
New-Item -ItemType Directory -Path $tls -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $qtRoot 'plugins/tls/qopensslbackend.dll') -Destination $tls -Force
foreach ($file in @('libssl-3-x64.dll','libcrypto-3-x64.dll')) {
    Copy-Item -LiteralPath (Join-Path $sourceRoot ('.deps/openssl-mingit-2.56.0.3/'+$file)) -Destination $build -Force
}
Copy-Item -LiteralPath (Join-Path $sourceRoot '.deps/openssl-mingit-2.56.0.3/OPENSSL-LICENSE.txt') -Destination $tls -Force
$resume = Join-Path $build 'resume'
New-Item -ItemType Directory -Path $resume -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $sourceRoot '.deps/pdfium-8111/bin/pdfium.dll') -Destination $resume -Force
Copy-Item -LiteralPath (Join-Path $sourceRoot '.deps/pdfium-8111/LICENSE') -Destination (Join-Path $resume 'PDFIUM-LICENSE.txt') -Force
Copy-Item -LiteralPath (Join-Path $sourceRoot '.deps/pdfium-8111/licenses') -Destination $resume -Recurse -Force
$destination = Join-Path $sourceRoot $(if ($Configuration -eq 'Release') { 'build-release' } else { 'build-native' })
New-Item -ItemType Directory -Path $destination -Force | Out-Null
if ($destination -ne $build) { Get-ChildItem -LiteralPath $build -Filter '*.exe' | Copy-Item -Destination $destination -Force }
if ($destination -ne $build) { Copy-Item -LiteralPath $resume -Destination $destination -Recurse -Force }
if ($destination -ne $build) {
    Copy-Item -LiteralPath $tls -Destination $destination -Recurse -Force
    foreach ($file in @('libssl-3-x64.dll','libcrypto-3-x64.dll')) {
        Copy-Item -LiteralPath (Join-Path $build $file) -Destination $destination -Force
    }
}
Set-Content -LiteralPath (Join-Path $destination 'build-location.txt') -Value $build
exit 0
