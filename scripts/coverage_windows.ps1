param(
    [string]$BuildDir = "build/build",
    [string]$Config = "Debug",
    [string]$Output = "coverage.xml"
)

if (-not (Get-Command OpenCppCoverage.exe -ErrorAction SilentlyContinue)) {
    Write-Error "OpenCppCoverage.exe not found in PATH."
    exit 1
}

$excluded = @(
    "$PWD\\visualizer\\*",
    "$PWD\\tests\\*",
    "$PWD\\SarTape2Generator\\src\\main.cpp"
)

OpenCppCoverage.exe `
    --modules SAR-Processor `
    --sources SAR-Processor `
    --excluded_sources $excluded `
    --export_type cobertura:$Output `
    -- `
    "$BuildDir\\sar_core_tests.exe"
