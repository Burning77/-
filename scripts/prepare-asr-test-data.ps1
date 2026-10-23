param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$dependency=Join-Path $root '.deps'
New-Item -ItemType Directory -Path $dependency -Force|Out-Null
$assets=@(
    @{Name='english-sentence.wav';Url='https://huggingface.co/csukuangfj/sherpa-onnx-streaming-zipformer-en-2023-06-26/resolve/main/test_wavs/0.wav';Hash='6BC58A4EFDF20DAAC252B6B1502632601A71EFE0308F6757DC1EDA34891A7E4F'},
    @{Name='local-asr/chinese-test.tar.bz2';Url='https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/spoken-language-identification-test-wavs.tar.bz2';Hash='2B0F4F7CADB0313B1D2A8A9887E7AF14DBDF8696083012BF1101C58F35AF6A79'}
)
foreach($asset in $assets){
    $file=Join-Path $dependency $asset.Name
    New-Item -ItemType Directory -Path (Split-Path $file -Parent) -Force|Out-Null
    if(!(Test-Path -LiteralPath $file)){Invoke-WebRequest -Uri $asset.Url -OutFile $file}
    if((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $asset.Hash){throw ('Checksum mismatch: '+$asset.Name)}
}
$chineseDir=Join-Path $dependency 'local-asr'
if(!(Test-Path -LiteralPath (Join-Path $chineseDir 'spoken-language-identification-test-wavs/zh-chinese.wav'))){
    & tar -xf (Join-Path $chineseDir 'chinese-test.tar.bz2') -C $chineseDir
    if($LASTEXITCODE -ne 0){throw 'Test audio extraction failed'}
}
# Only synthetic test speech; no user recordings or credentials are read.
Add-Type -AssemblyName System.Speech
$speaker=New-Object System.Speech.Synthesis.SpeechSynthesizer
try{
    $voice=$speaker.GetInstalledVoices()|Where-Object {$_.VoiceInfo.Culture.Name -like 'en-*'}|Select-Object -First 1
    if(!$voice){throw 'An installed Windows English voice is required for the technical-word test'}
    $speaker.SelectVoice($voice.VoiceInfo.Name)
    $format=New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(16000,[System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen,[System.Speech.AudioFormat.AudioChannel]::Mono)
    $speaker.SetOutputToWaveFile((Join-Path $dependency 'english-technical.wav'),$format)
    $speaker.Speak('This project uses direct memory access. Please explain a mutex and a semaphore.')
}finally{$speaker.Dispose()}
Write-Output 'ASR test audio ready'
