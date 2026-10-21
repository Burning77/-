param([string]$Filter = '.*')
$env:PATH = 'D:\Qt\6.11.2\mingw_64\bin;D:\Qt\Tools\mingw1310_64\bin;' + $env:PATH
$root = Split-Path $PSScriptRoot -Parent
$build = (Get-Content -LiteralPath (Join-Path $root 'build-native/build-location.txt') -Raw).Trim()
$started = Get-Date
& 'D:/Qt/Tools/CMake_64/bin/ctest.exe' --test-dir $build --output-on-failure -R $Filter
$result = $LASTEXITCODE
Get-ChildItem -LiteralPath $build -Filter 'tst_*.txt' | Where-Object { $_.LastWriteTime -ge $started.AddSeconds(-1) } | ForEach-Object { Get-Content -LiteralPath $_.FullName -Tail 7 }
exit $result
