param(
  [string]$ProjectDir = (Join-Path $PSScriptRoot 'esp32_health_monitor'),
  [string]$PublicFirmwareDir = (Join-Path $PSScriptRoot '..\public\firmware')
)

$ErrorActionPreference = 'Stop'
$ProjectDir = [System.IO.Path]::GetFullPath($ProjectDir)
$PublicFirmwareDir = [System.IO.Path]::GetFullPath($PublicFirmwareDir)
$PublicRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\public'))
if (-not $PublicFirmwareDir.StartsWith($PublicRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
  throw "Output must stay under $PublicRoot"
}

Push-Location $ProjectDir
try {
  & (Join-Path $env:USERPROFILE '.platformio\penv\Scripts\pio.exe') run
  if ($LASTEXITCODE -ne 0) { throw 'PlatformIO build failed.' }
} finally {
  Pop-Location
}

$BuildDir = Join-Path $ProjectDir '.pio\build\esp32dev'
New-Item -ItemType Directory -Path $PublicFirmwareDir -Force | Out-Null
Copy-Item (Join-Path $BuildDir 'bootloader.bin') (Join-Path $PublicFirmwareDir 'bootloader.bin') -Force
Copy-Item (Join-Path $BuildDir 'partitions.bin') (Join-Path $PublicFirmwareDir 'partitions.bin') -Force
Copy-Item (Join-Path $BuildDir 'boot_app0.bin') (Join-Path $PublicFirmwareDir 'boot_app0.bin') -Force
Copy-Item (Join-Path $BuildDir 'firmware.bin') (Join-Path $PublicFirmwareDir 'firmware.bin') -Force
Write-Host "Browser firmware parts copied to $PublicFirmwareDir"
