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

$ocArgs = @(
    "--modules", "SAR-Processor",
    "--sources", "SAR-Processor",
    "--export_type", "cobertura:$Output"
)

foreach ($pattern in $excluded) {
    $ocArgs += "--excluded_sources"
    $ocArgs += $pattern
}

$ocArgs += "--"
$ocArgs += "$BuildDir\\$Config\\sar_core_tests.exe"

OpenCppCoverage.exe @ocArgs
