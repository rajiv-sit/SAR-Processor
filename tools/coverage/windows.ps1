param(
    [string]$BuildDir = "build/build",
    [string]$Config = "Debug",
    [string]$Output = "coverage.xml"
)

if (-not (Get-Command OpenCppCoverage.exe -ErrorAction SilentlyContinue)) {
    Write-Error "OpenCppCoverage.exe not found in PATH."
    exit 1
}

OpenCppCoverage.exe `
    --modules SAR-Processor `
    --sources SAR-Processor `
    --export_type cobertura:$Output `
    -- `
    $BuildDir\\sar_core_tests.exe
