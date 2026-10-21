param([string]$BuildRoot = $env:INTERVIEW_BUILD_ROOT)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
& (Join-Path $PSScriptRoot 'build.ps1') -Configuration Release -BuildRoot $BuildRoot
if($LASTEXITCODE -ne 0){throw 'Release build failed'}
$nativeBuild=(Get-Content -LiteralPath (Join-Path $root 'build-release/build-location.txt') -Raw).Trim()
$stage=Join-Path (Split-Path $nativeBuild -Parent) ('package-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'/InterviewAssistant')
New-Item -ItemType Directory -Path $stage -Force|Out-Null
Copy-Item -LiteralPath (Join-Path $nativeBuild 'InterviewAssistant.exe') -Destination $stage -Force
$env:PATH='D:\Qt\6.11.2\mingw_64\bin;D:\Qt\Tools\mingw1310_64\bin;'+$env:PATH
& 'D:/Qt/6.11.2/mingw_64/bin/windeployqt.exe' --release --compiler-runtime --no-translations --no-quick-import --dir $stage (Join-Path $stage 'InterviewAssistant.exe')
if($LASTEXITCODE -ne 0){throw 'Qt deployment failed'}
Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination $stage
$destination=Join-Path $root 'dist'
New-Item -ItemType Directory -Path $destination -Force|Out-Null
Copy-Item -LiteralPath $stage -Destination $destination -Recurse -Force
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath (Join-Path $destination 'InterviewAssistant-win64.zip') -Force
Write-Output (Join-Path $destination 'InterviewAssistant/InterviewAssistant.exe')
