param([string]$BuildRoot = $env:INTERVIEW_BUILD_ROOT,
      [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]*$')][string]$PackageName = 'InterviewAssistant')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
& (Join-Path $PSScriptRoot 'prepare-local-asr.ps1')
& (Join-Path $PSScriptRoot 'build.ps1') -Configuration Release -BuildRoot $BuildRoot
if($LASTEXITCODE -ne 0){throw 'Release build failed'}
$nativeBuild=(Get-Content -LiteralPath (Join-Path $root 'build-release/build-location.txt') -Raw).Trim()
$stage=Join-Path (Split-Path $nativeBuild -Parent) ('package-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'/'+$PackageName)
New-Item -ItemType Directory -Path $stage -Force|Out-Null
Copy-Item -LiteralPath (Join-Path $nativeBuild 'InterviewAssistant.exe') -Destination $stage -Force
$env:PATH='D:\Qt\6.11.2\mingw_64\bin;D:\Qt\Tools\mingw1310_64\bin;'+$env:PATH
& 'D:/Qt/6.11.2/mingw_64/bin/windeployqt.exe' --release --compiler-runtime --no-translations --no-quick-import --dir $stage (Join-Path $stage 'InterviewAssistant.exe')
if($LASTEXITCODE -ne 0){throw 'Qt deployment failed'}
Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination $stage
$asr=Join-Path $stage 'local-asr'
New-Item -ItemType Directory -Path $asr -Force|Out-Null
Copy-Item -LiteralPath (Join-Path $root '.deps/local-asr/vosk-win64-0.3.45/libvosk.dll') -Destination $asr -Force
Copy-Item -LiteralPath (Join-Path $root '.deps/local-asr/vosk-model-small-cn-0.22') -Destination (Join-Path $asr 'model') -Recurse -Force
Copy-Item -LiteralPath (Join-Path $root '.deps/local-asr/VOSK-LICENSE.txt') -Destination $asr -Force
Copy-Item -LiteralPath (Join-Path $root '.deps/local-asr/OPENBLAS-LICENSE.txt') -Destination $asr -Force
Copy-Item -LiteralPath (Join-Path $root 'THIRD_PARTY.md') -Destination $stage -Force
$destination=Join-Path $root 'dist'
New-Item -ItemType Directory -Path $destination -Force|Out-Null
Copy-Item -LiteralPath $stage -Destination $destination -Recurse -Force
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath (Join-Path $destination ($PackageName+'-win64.zip')) -Force
Write-Output (Join-Path $destination ($PackageName+'/InterviewAssistant.exe'))
