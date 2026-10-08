[CmdletBinding()]
param(
    [string]$Compiler,
    [string]$Output,
    [switch]$Test
)

$ErrorActionPreference = 'Stop'
if (-not $Output) { $Output = Join-Path $PSScriptRoot 'fetch.exe' }
if (-not $Compiler) {
    $command = Get-Command 'clang.exe' -ErrorAction SilentlyContinue
    if ($command) { $Compiler = $command.Source }
    else {
        $Compiler = Get-ChildItem -Path (Join-Path $PSScriptRoot '.tools\llvm-mingw-*\bin\clang.exe') -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
    }
}
if (-not $Compiler -or -not (Test-Path -LiteralPath $Compiler)) {
    throw 'LLVM/MinGW is required. Unpack its UCRT Windows archive and pass -Compiler <path-to-clang.exe>. See docs/windows.md.'
}
$version = (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'VERSION') -Raw).Trim()
$flags = @('--target=x86_64-w64-windows-gnu', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror')
$source = Join-Path $PSScriptRoot 'fetch.c'
$metadata = [System.IO.Path]::GetTempFileName()
try {
    $escapedVersion = $version.Replace('\', '\\').Replace('"', '\"')
    $header = "#define FETCH_VERSION `"$escapedVersion`"`n#define FETCH_CODENAME `"Overclocked ASCII`"`n#define FETCH_ARCH `"x86_64`"`n#define FETCH_OS `"Windows`"`n"
    [System.IO.File]::WriteAllText($metadata, $header, (New-Object System.Text.UTF8Encoding($false)))
    & $Compiler @flags '-include' $metadata '-municode' $source '-o' $Output '-ladvapi32'
    if ($LASTEXITCODE -ne 0) { throw "fetch build failed with exit code $LASTEXITCODE" }
} finally {
    [System.IO.File]::Delete($metadata)
}
Write-Output "Built $Output"
if ($Test) {
    $testBinary = Join-Path $PSScriptRoot 'tests\windows-tests.exe'
    & $Compiler @flags (Join-Path $PSScriptRoot 'tests\windows.c') '-o' $testBinary '-ladvapi32'
    if ($LASTEXITCODE -ne 0) { throw "test build failed with exit code $LASTEXITCODE" }
    & (Join-Path $PSScriptRoot 'tests\windows.ps1') -Executable $Output -Compiler $Compiler
}
