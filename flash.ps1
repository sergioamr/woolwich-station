# Flash ESP32 ePaper application
# Usage: .\flash.ps1 [COM_PORT]
# Example: .\flash.ps1 COM21

param(
    [string]$Port = "COM21"
)

$ErrorActionPreference = "Stop"

$IDF_PATH = "C:\Users\ryanc\esp-idf"
$IDF_TOOLS_PATH = "E:\Espressif"
$ProjectDir = $PSScriptRoot

# Add Espressif tools to PATH first (before system cmake)
$env:IDF_PATH = $IDF_PATH
$env:IDF_TOOLS_PATH = $IDF_TOOLS_PATH
$env:PATH = "$IDF_TOOLS_PATH\tools\idf-python\3.11.2;$IDF_TOOLS_PATH\tools\idf-git\2.44.0\cmd;$IDF_TOOLS_PATH\tools\cmake\3.13.4\bin;$IDF_TOOLS_PATH\tools\ninja\1.9.0;$env:PATH"

# Set port via env var (idf.py reads ESPPORT)
$env:ESPPORT = $Port

# Initialize ESP-IDF environment
. "$IDF_PATH\export.ps1" 2>&1 | Out-Null

# Flash (no -p needed, uses ESPPORT)
Set-Location $ProjectDir
Write-Host "Flashing to $Port..." -ForegroundColor Cyan
idf.py flash
