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
Copy-Item -LiteralPath (Join-Path $nativeBuild 'ResumeReader.exe') -Destination $stage -Force
Copy-Item -LiteralPath (Join-Path $nativeBuild 'resume') -Destination $stage -Recurse -Force
$env:PATH='D:\Qt\6.11.2\mingw_64\bin;D:\Qt\Tools\mingw1310_64\bin;'+$env:PATH
& 'D:/Qt/6.11.2/mingw_64/bin/windeployqt.exe' --release --compiler-runtime --no-translations --no-quick-import --dir $stage (Join-Path $stage 'InterviewAssistant.exe')
if($LASTEXITCODE -ne 0){throw 'Qt deployment failed'}
Copy-Item -LiteralPath (Join-Path $nativeBuild 'tls') -Destination $stage -Recurse -Force
foreach($file in @('libssl-3-x64.dll','libcrypto-3-x64.dll')){
    Copy-Item -LiteralPath (Join-Path $nativeBuild $file) -Destination $stage -Force
}
Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination $stage
$asr=Join-Path $stage 'local-asr'
New-Item -ItemType Directory -Path $asr -Force|Out-Null
$runtime=Join-Path $root '.deps/sherpa-onnx-v1.13.8-win-x64-shared-MT-Release-no-tts/lib'
foreach($dll in @('sherpa-onnx-c-api.dll','onnxruntime.dll','onnxruntime_providers_shared.dll')){
    Copy-Item -LiteralPath (Join-Path $runtime $dll) -Destination $asr -Force
}
$model=Join-Path $asr 'bilingual'
New-Item -ItemType Directory -Path $model -Force|Out-Null
foreach($file in @('encoder-epoch-99-avg-1.int8.onnx','decoder-epoch-99-avg-1.onnx','joiner-epoch-99-avg-1.int8.onnx','tokens.txt','README.md')){
    Copy-Item -LiteralPath (Join-Path $root ('.deps/sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20/'+$file)) -Destination $model -Force
}
foreach($license in @('SHERPA-ONNX-LICENSE.txt','ONNX-RUNTIME-LICENSE.txt','ONNX-THIRD-PARTY-NOTICES.txt')){
    Copy-Item -LiteralPath (Join-Path $root ('.deps/'+$license)) -Destination $asr -Force
}
Copy-Item -LiteralPath (Join-Path $root 'THIRD_PARTY.md') -Destination $stage -Force
$destination=Join-Path $root 'dist'
New-Item -ItemType Directory -Path $destination -Force|Out-Null
Copy-Item -LiteralPath $stage -Destination $destination -Recurse -Force
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath (Join-Path $destination ($PackageName+'-win64.zip')) -Force
Write-Output (Join-Path $destination ($PackageName+'/InterviewAssistant.exe'))
