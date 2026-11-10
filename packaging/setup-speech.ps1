param(
    [string]$BuildDirectory = "build",
    [string]$ArchivePath,
    [string]$ModelPath
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$build = if ([System.IO.Path]::IsPathRooted($BuildDirectory)) {
    $BuildDirectory
} else {
    Join-Path $root $BuildDirectory
}
if (-not (Test-Path -LiteralPath $build -PathType Container)) {
    throw "Build directory does not exist: $build"
}
$build = (Resolve-Path -LiteralPath $build).Path
if (Get-Process | Where-Object { $_.ProcessName -eq "DynamicWindows" }) {
    throw "Exit DynamicWindows before installing the speech runtime."
}

$runtimeHash = "b1514ebc099765e39fa37eb780b92a140a94c86bb0b3b3d98226b38825979732"
$modelHash = "be07e048e1e599ad46341c8d2a135645097a538221678b7acdd1b1919c6e1b21"
$runtimeUrl = "https://github.com/ggml-org/whisper.cpp/releases/download/v1.8.2/whisper-bin-x64.zip"
$modelUrl = "https://huggingface.co/ggerganov/whisper.cpp/resolve/5359861c739e955e79d9a303bcbc70fb988958b1/ggml-tiny.bin"

function Get-VerifiedFile([string]$Path, [string]$ExpectedHash) {
    $file = Get-Item -LiteralPath $Path
    if ($file.PSIsContainer -or (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash -ne $ExpectedHash) {
        throw "SHA-256 verification failed: $Path"
    }
    return $file.FullName
}

$work = Join-Path ([System.IO.Path]::GetTempPath()) ("DynamicWindows-speech-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $work | Out-Null
if (-not $ArchivePath) {
    $ArchivePath = Join-Path $work "runtime.zip"
    & curl.exe --fail --location --silent --show-error --max-time 120 $runtimeUrl -o $ArchivePath
    if ($LASTEXITCODE -ne 0) {
        throw "Whisper runtime download failed. Partial file retained at $ArchivePath"
    }
}
$ArchivePath = Get-VerifiedFile $ArchivePath $runtimeHash
if (-not $ModelPath) {
    $ModelPath = Join-Path $work "ggml-tiny.bin"
    & curl.exe --fail --location --silent --show-error --max-time 300 $modelUrl -o $ModelPath
    if ($LASTEXITCODE -ne 0) {
        throw "Whisper model download failed. Partial file retained at $ModelPath"
    }
}
$ModelPath = Get-VerifiedFile $ModelPath $modelHash

$unpacked = Join-Path $work "runtime"
Expand-Archive -LiteralPath $ArchivePath -DestinationPath $unpacked
$files = @("whisper.dll", "ggml.dll", "ggml-base.dll", "ggml-cpu.dll")
foreach ($name in $files) {
    if (-not (Test-Path -LiteralPath (Join-Path $unpacked "Release\$name") -PathType Leaf)) {
        throw "Pinned runtime archive is missing $name"
    }
}

$system = Join-Path $env:windir "System32"
$missing = @("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll", "vcomp140.dll") |
    Where-Object { -not (Test-Path -LiteralPath (Join-Path $system $_) -PathType Leaf) }
if ($missing) {
    throw "Install Microsoft's Visual C++ 2015-2022 x64 Redistributable first: https://aka.ms/vs/17/release/vc_redist.x64.exe. Missing: $($missing -join ', '). Verified downloads remain at $work"
}

$speech = Join-Path $build "speech"
if (Test-Path -LiteralPath $speech) {
    Get-ChildItem -LiteralPath $speech | Select-Object Name, Length
} else {
    New-Item -ItemType Directory -Path $speech | Out-Null
}
foreach ($name in $files) {
    $source = Join-Path $unpacked "Release\$name"
    $target = Join-Path $speech $name
    Copy-Item -LiteralPath $source -Destination $target -Force
    if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash) {
        throw "Installed runtime checksum differs: $target"
    }
}
$installedModel = Join-Path $speech "ggml-tiny.bin"
if (-not (Test-Path -LiteralPath $installedModel -PathType Leaf) -or
    (Get-FileHash -LiteralPath $installedModel -Algorithm SHA256).Hash -ne $modelHash) {
    Copy-Item -LiteralPath $ModelPath -Destination $installedModel -Force
}
$null = Get-VerifiedFile $installedModel $modelHash
foreach ($name in @("whisper-LICENSE.txt", "whisper-model-LICENSE.txt")) {
    Copy-Item -LiteralPath (Join-Path $root "resources\$name") -Destination (Join-Path $speech $name) -Force
}
Write-Host "Installed verified Whisper 1.8.2 and multilingual tiny model in $speech"
Write-Host "No microphone access or transcription was performed. Download/extraction files retained at $work"
