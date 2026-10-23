param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$dependency=Join-Path $root '.deps'
New-Item -ItemType Directory -Path $dependency -Force | Out-Null
$assets=@(
    @{Name='sherpa-runtime.tar.bz2';Url='https://github.com/k2-fsa/sherpa-onnx/releases/download/v1.13.8/sherpa-onnx-v1.13.8-win-x64-shared-MT-Release-no-tts.tar.bz2';Hash='4B0A94F7B5C606B1B64A19A831C2127559E4B3D34E195465EBC7BE73D9ED4783';Marker='sherpa-onnx-v1.13.8-win-x64-shared-MT-Release-no-tts/lib/sherpa-onnx-c-api.dll'},
    @{Name='bilingual-model.tar.bz2';Url='https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20.tar.bz2';Hash='27FFBD9EE24AD186D99ACC2F6354D7992B27BCAB490812510665FA8F9389C5F8';Marker='sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20/tokens.txt'}
)
foreach($asset in $assets){
    $archive=Join-Path $dependency $asset.Name
    if(!(Test-Path -LiteralPath $archive)){Invoke-WebRequest -Uri $asset.Url -OutFile $archive}
    if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $asset.Hash){throw ('Checksum mismatch: '+$asset.Name)}
    if(!(Test-Path -LiteralPath (Join-Path $dependency $asset.Marker))){
        & tar -xf $archive -C $dependency
        if($LASTEXITCODE -ne 0){throw ('Extract failed: '+$asset.Name)}
    }
}
$licenses=@{
    'SHERPA-ONNX-LICENSE.txt'='https://raw.githubusercontent.com/k2-fsa/sherpa-onnx/v1.13.8/LICENSE'
    'ONNX-RUNTIME-LICENSE.txt'='https://raw.githubusercontent.com/microsoft/onnxruntime/v1.28.2/LICENSE'
    'ONNX-THIRD-PARTY-NOTICES.txt'='https://raw.githubusercontent.com/microsoft/onnxruntime/v1.28.2/ThirdPartyNotices.txt'
}
foreach($name in $licenses.Keys){
    $file=Join-Path $dependency $name
    if(!(Test-Path -LiteralPath $file)){Invoke-WebRequest -Uri $licenses[$name] -OutFile $file}
}
Write-Output ('Local bilingual ASR dependencies ready: '+$dependency)
