param([string]$Filter = '.*', [ValidateSet('Debug','Release')][string]$Configuration = 'Debug')
$env:PATH = 'D:\Qt\6.11.2\mingw_64\bin;D:\Qt\Tools\mingw1310_64\bin;' + $env:PATH
$root = Split-Path $PSScriptRoot -Parent
if(Test-Path -LiteralPath (Join-Path $root '.deps/local-asr/vosk-model-small-cn-0.22/am/final.mdl')){$env:INTERVIEW_LOCAL_ASR_ROOT=Join-Path $root '.deps/local-asr'}
if(Test-Path -LiteralPath (Join-Path $root '.deps/local-asr/spoken-language-identification-test-wavs/zh-chinese.wav')){$env:INTERVIEW_CHINESE_TEST_WAV=Join-Path $root '.deps/local-asr/spoken-language-identification-test-wavs/zh-chinese.wav'}
$buildFolder = if ($Configuration -eq 'Release') { 'build-release' } else { 'build-native' }
$build = (Get-Content -LiteralPath (Join-Path $root ($buildFolder+'/build-location.txt')) -Raw).Trim()
$started = Get-Date
& 'D:/Qt/Tools/CMake_64/bin/ctest.exe' --test-dir $build --output-on-failure -R $Filter
$result = $LASTEXITCODE
Get-ChildItem -LiteralPath $build -Filter 'tst_*.txt' | Where-Object { $_.LastWriteTime -ge $started.AddSeconds(-1) } | ForEach-Object { Get-Content -LiteralPath $_.FullName -Tail 7 }
exit $result
