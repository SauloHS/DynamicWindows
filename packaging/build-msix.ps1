param(
    [string]$BuildDirectory = "build-notifications",
    [string]$OutputDirectory = "dist",
    [string]$Publisher = "CN=DynamicWindows Development"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $root "$BuildDirectory\DynamicWindows.exe"
$manifestTemplate = Join-Path $PSScriptRoot "AppxManifest.xml"
if (-not (Test-Path -LiteralPath $executable)) {
    throw "Build DynamicWindows.exe before packaging."
}
if (-not (Test-Path -LiteralPath $manifestTemplate)) {
    throw "Packaging inputs are missing."
}

$sdkRoot = "${env:ProgramFiles(x86)}\Windows Kits\10\bin"
$sdkVersion = Get-ChildItem -LiteralPath $sdkRoot -Directory |
    Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName "x64\makeappx.exe") } |
    Sort-Object Name -Descending |
    Select-Object -First 1
if (-not $sdkVersion) {
    throw "Windows SDK makeappx.exe was not found."
}
$makeAppx = Join-Path $sdkVersion.FullName "x64\makeappx.exe"

$output = Join-Path $root $OutputDirectory
$stage = Join-Path $output "msix-stage"
$assets = Join-Path $stage "Assets"
New-Item -ItemType Directory -Force -Path $assets | Out-Null
Copy-Item -LiteralPath $executable -Destination (Join-Path $stage "DynamicWindows.exe")
Copy-Item -LiteralPath (Join-Path $root "LICENSE") -Destination (Join-Path $stage "LICENSE.txt")
$speech = Join-Path (Split-Path -Parent $executable) "speech"
if (Test-Path -LiteralPath $speech -PathType Container) {
    $speechStage = Join-Path $stage "speech"
    New-Item -ItemType Directory -Force -Path $speechStage | Out-Null
    foreach ($name in @("whisper.dll", "ggml.dll", "ggml-base.dll", "ggml-cpu.dll",
            "ggml-tiny.bin", "whisper-LICENSE.txt", "whisper-model-LICENSE.txt")) {
        $source = Join-Path $speech $name
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw "Speech runtime is incomplete: $source. Run packaging/setup-speech.ps1 first."
        }
        Copy-Item -LiteralPath $source -Destination (Join-Path $speechStage $name)
    }
}

$manifest = [System.IO.File]::ReadAllText($manifestTemplate)
$manifest = $manifest.Replace('Publisher="CN=DynamicWindows Development"', "Publisher=`"$Publisher`"")
[System.IO.File]::WriteAllText((Join-Path $stage "AppxManifest.xml"), $manifest,
    [System.Text.UTF8Encoding]::new($false))

Add-Type -AssemblyName System.Drawing
function Write-Logo([string]$OutputPath, [int]$Width, [int]$Height) {
    $bitmap = [System.Drawing.Bitmap]::new($Width, $Height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.Clear([System.Drawing.Color]::FromArgb(5, 5, 6))
    $pillWidth = [int]($Width * 0.70)
    $pillHeight = [int]([Math]::Max(8, $Height * 0.24))
    $x = [int](($Width - $pillWidth) / 2)
    $y = [int](($Height - $pillHeight) / 2)
    $radius = [int]($pillHeight / 2)
    $logoPath = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $diameter = $radius * 2
    $logoPath.AddArc($x, $y, $diameter, $diameter, 90, 180)
    $logoPath.AddArc($x + $pillWidth - $diameter, $y, $diameter, $diameter, 270, 180)
    $logoPath.CloseFigure()
    $brush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(16, 16, 20))
    $graphics.FillPath($brush, $logoPath)
    $dot = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(157, 216, 255))
    $dotSize = [int]([Math]::Max(3, $pillHeight * 0.25))
    $graphics.FillEllipse($dot, $x + $pillWidth - $radius - $dotSize / 2,
        $y + $radius - $dotSize / 2, $dotSize, $dotSize)
    $bitmap.Save($OutputPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $dot.Dispose()
    $brush.Dispose()
    $logoPath.Dispose()
    $graphics.Dispose()
    $bitmap.Dispose()
}

Write-Logo (Join-Path $assets "StoreLogo.png") 50 50
Write-Logo (Join-Path $assets "Square44x44Logo.png") 44 44
Write-Logo (Join-Path $assets "Square150x150Logo.png") 150 150
Write-Logo (Join-Path $assets "Square310x310Logo.png") 310 310
Write-Logo (Join-Path $assets "Wide310x150Logo.png") 310 150

$package = Join-Path $output "DynamicWindows-0.1.0-x64-unsigned.msix"
if (Test-Path -LiteralPath $package) {
    Remove-Item -LiteralPath $package -Force -Confirm:$false
}
& $makeAppx pack /d $stage /p $package /o
if ($LASTEXITCODE -ne 0) {
    throw "makeappx.exe failed."
}
Write-Host "Created unsigned package: $package"
Write-Host "Sign it with a certificate whose subject exactly matches: $Publisher"
