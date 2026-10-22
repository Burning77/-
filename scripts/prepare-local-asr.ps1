param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$dependency=Join-Path $root '.deps/local-asr'
New-Item -ItemType Directory -Path $dependency -Force | Out-Null
$assets=@(
    @{Name='runtime.zip';Url='https://github.com/alphacep/vosk-api/releases/download/v0.3.45/vosk-win64-0.3.45.zip';Hash='F1DCC9CCA460630F81EA8F71794F69C80BED6556D2A4E6237B5785E1D2DFF34B';Marker='vosk-win64-0.3.45/libvosk.dll'},
    @{Name='model.zip';Url='https://alphacephei.com/vosk/models/vosk-model-small-cn-0.22.zip';Hash='3AF8B0E7E0F835AE9D414CE5DF580237A3CFB08D586C9FBBB0F7FF29AD5B14BA';Marker='vosk-model-small-cn-0.22/am/final.mdl'}
)
foreach($asset in $assets){
    $archive=Join-Path $dependency $asset.Name
    if(!(Test-Path -LiteralPath $archive)){Invoke-WebRequest -Uri $asset.Url -OutFile $archive}
    if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $asset.Hash){throw ('Checksum mismatch: '+$asset.Name)}
    if(!(Test-Path -LiteralPath (Join-Path $dependency $asset.Marker))){Expand-Archive -LiteralPath $archive -DestinationPath $dependency -Force}
}
$license=Join-Path $dependency 'VOSK-LICENSE.txt'
if(!(Test-Path -LiteralPath $license)){Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/alphacep/vosk-api/v0.3.45/COPYING' -OutFile $license}
$blasLicense=Join-Path $dependency 'OPENBLAS-LICENSE.txt'
if(!(Test-Path -LiteralPath $blasLicense)){Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/OpenMathLib/OpenBLAS/v0.3.13/LICENSE' -OutFile $blasLicense}
Write-Output ('Local ASR dependencies ready: '+$dependency)
