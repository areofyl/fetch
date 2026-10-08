[CmdletBinding()]
param([Parameter(Mandatory)][string]$Executable, [Parameter(Mandatory)][string]$Compiler)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$work = Join-Path $PSScriptRoot '.work'
$bin = Join-Path $work 'bin space'
$unicode = [string][char]0x00FC
$appdata = Join-Path $work "appdata $unicode"
New-Item -ItemType Directory -Force -Path $bin, (Join-Path $appdata 'fetch') | Out-Null
$fixture = Join-Path $bin 'fastfetch.exe'
& $Compiler '--target=x86_64-w64-windows-gnu' '-std=c11' '-O2' '-Wall' '-Wextra' '-Werror' '-municode' (Join-Path $PSScriptRoot 'fake-fastfetch.c') '-o' $fixture
if ($LASTEXITCODE -ne 0) { throw 'Fastfetch fixture build failed' }
$console = Join-Path $PSScriptRoot 'windows-console.exe'
& $Compiler '--target=x86_64-w64-windows-gnu' '-std=c11' '-O2' '-Wall' '-Wextra' '-Werror' '-municode' (Join-Path $PSScriptRoot 'windows-console.c') '-o' $console '-ladvapi32'
if ($LASTEXITCODE -ne 0) { throw 'Console test build failed' }

function Quote-Argument([string]$Value) {
    '"' + [regex]::Replace([regex]::Replace($Value, '(\\*)"', '$1$1\"'), '(\\+)$', '$1$1') + '"'
}
function Run-Program([string]$Program, [string[]]$Arguments, [hashtable]$Environment = @{}, [int]$Timeout = 15000) {
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $Program
    $info.Arguments = ($Arguments | ForEach-Object { Quote-Argument $_ }) -join ' '
    $info.WorkingDirectory = $root
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.StandardOutputEncoding = New-Object System.Text.UTF8Encoding($false)
    $info.StandardErrorEncoding = New-Object System.Text.UTF8Encoding($false)
    $info.EnvironmentVariables['APPDATA'] = $appdata
    foreach ($key in $Environment.Keys) { $info.EnvironmentVariables[$key] = [string]$Environment[$key] }
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $info
    $null = $process.Start()
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($Timeout)) { $process.Kill(); throw "Timed out: $Program $($info.Arguments)" }
    $result = @{ Code = $process.ExitCode; Out = $stdout.Result; Err = $stderr.Result }
    $process.Dispose()
    return $result
}
function Assert([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    $script:checks++
}
$checks = 0
[System.IO.File]::WriteAllText((Join-Path $appdata 'fetch\config'), "os`nhost`nkernel`nuptime`nshell`ndisplay`nwm`nterminal`ncpu`ngpu`nmemory`nswap`ndisk`nip`nlocale`n")
[System.IO.File]::WriteAllText((Join-Path $appdata 'fetch\logo.txt'), '')
$unit = Run-Program (Join-Path $PSScriptRoot 'windows-tests.exe') @($fixture) @{} 20000
Assert ($unit.Code -eq 0) "Unit tests failed: $($unit.Out) $($unit.Err)"
Write-Output $unit.Out.Trim()

$real = Run-Program $Executable @('--frames', '2')
Assert ($real.Code -eq 0 -and $real.Out -match 'OS: Windows' -and $real.Out -match 'Memory:') "Native Windows smoke test failed: $($real.Err)"
if (Get-Command fastfetch.exe -ErrorAction SilentlyContinue) {
    Assert ($real.Out -match 'CPU:' -and $real.Err.Length -eq 0) "Installed Fastfetch smoke test failed: $($real.Err)"
    Assert ($real.Out.Contains('Disk (C:\):')) 'Disk mountpoint labels were lost'
}
Assert (-not $real.Out.Contains([char]27) -and -not $real.Out.Contains([char]0)) 'Redirected output contains terminal escapes or NUL bytes'
$help = Run-Program $Executable @('--help')
Assert ($help.Code -eq 0 -and $help.Out.Contains('%APPDATA%\fetch\config')) 'Help does not describe Windows configuration'
$version = Run-Program $Executable @('--version')
Assert ($version.Code -eq 0 -and $version.Out -match 'Windows') 'Version lacks the Windows build target'
$invalid = @(@('--frames', '-1'), @('--frames', 'junk'), @('--frames', '2147483648'), @('--height', '0'),
    @('--size', 'nan'), @('--depth', 'inf'), @('--speed', '1junk'), @('--logo'), @('--unknown'), @('--shading-mode', 'unknown'))
foreach ($arguments in $invalid) {
    $result = Run-Program $Executable $arguments
    Assert ($result.Code -ne 0 -and $result.Err.Length -gt 0) "Invalid option accepted: $arguments"
}
$envFixture = @{ PATH = $bin }
$missing = Run-Program $Executable @('--frames', '1') @{ PATH = '' }
Assert ($missing.Code -eq 0 -and $missing.Err -match 'not found' -and $missing.Out -match 'Memory:') 'Missing Fastfetch fallback failed'
$failed = Run-Program $Executable @('--frames', '1') @{ PATH = $bin; FETCH_TEST_MODE = 'fail' }
Assert ($failed.Code -eq 0 -and $failed.Err -match 'failed' -and $failed.Out -match 'OS: Windows') 'Failed Fastfetch fallback failed'
$badLogo = Run-Program $Executable @('--logo', 'Windows & echo injected > bad.txt', '--no-info', '--frames', '1') $envFixture
Assert ($badLogo.Code -eq 0 -and $badLogo.Err -match 'could not find' -and -not (Test-Path -LiteralPath (Join-Path $root 'bad.txt'))) 'Invalid logo was not rejected safely'
$namedLogo = Run-Program $Executable @('--logo', 'test logo', '--no-info', '--frames', '1') $envFixture
Assert ($namedLogo.Code -eq 0 -and $namedLogo.Err.Length -eq 0) 'A valid logo name containing spaces was lost'
$config = Join-Path $appdata 'fetch\config'
$utf8 = New-Object System.Text.UTF8Encoding($true)
[System.IO.File]::WriteAllText($config, "cpu`n---`nos`ngpu`nuptime`nmemory`ncustom_Unicode=Gr${unicode}sse`nheight=30`nsize=2`ndepth=2`nspeed=2`n", $utf8)
$ordered = Run-Program $Executable @('--frames', '1', '--height', '20', '--size', '1', '--depth', '1', '--speed', '1') $envFixture
Assert ($ordered.Code -eq 0 -and $ordered.Out.IndexOf('CPU:') -lt $ordered.Out.IndexOf('OS:') -and $ordered.Out -match 'GPU Two') 'Configuration field order or repeated GPU fields were lost'
Assert ($ordered.Out.Contains("Gr${unicode}sse")) 'Unicode APPDATA path or BOM configuration was not loaded'
[System.IO.File]::WriteAllText((Join-Path $appdata 'fetch\logo.txt'), "# distro: windows`n  @@  `n@@@@@@`n  @@  `n", $utf8)
$log = Join-Path $work 'calls.txt'
[System.IO.File]::WriteAllText($log, '')
$custom = Run-Program $Executable @('--no-info', '--infinite') @{ PATH = $bin; FETCH_TEST_LOG = $log }
Assert ($custom.Code -eq 0 -and $custom.Out.Length -gt 0 -and (Get-Content -LiteralPath $log).Count -eq 0) 'Custom Unicode-path logo or single redirected frame failed'
foreach ($mode in @('ascii', 'blocks', 'sextants')) {
    $result = Run-Program $Executable @('--no-info', '--shading-mode', $mode, '--frames', '1') $envFixture
    Assert ($result.Code -eq 0 -and $result.Out.Length -gt 0) "Shading mode failed: $mode"
}
$glyph = [string][char]0x2588
$unicodeRamp = Run-Program $Executable @('--no-info', '--shading-chars', $glyph, '--frames', '1') $envFixture
Assert ($unicodeRamp.Code -eq 0 -and $unicodeRamp.Out.Contains($glyph)) 'Unicode shading characters were lost in CLI argument conversion'
[System.IO.File]::WriteAllText($config, "size=1`ndepth=1`nspeed=1`nheight=20`n")
$baseline = Run-Program $Executable @('--no-info', '--frames', '1') $envFixture
[System.IO.File]::WriteAllText($config, "size=2`ndepth=2`nspeed=2`nheight=30`n")
$overridden = Run-Program $Executable @('--no-info', '--frames', '1', '--size', '1', '--depth', '1', '--speed', '1', '--height', '20') $envFixture
Assert ($baseline.Out -eq $overridden.Out) 'CLI settings did not override config values'
[System.IO.File]::WriteAllText($config, "os`nuptime`nmemory`ncpu`n")
[System.IO.File]::WriteAllText($log, '')
$consoleResult = Run-Program $console @($Executable) @{ PATH = $bin; FETCH_TEST_LOG = $log } 35000
$report = Get-Content -LiteralPath (Join-Path $work 'console-report.txt') -Raw
Assert ($consoleResult.Code -eq 0) "Console integration failed: $report"
Assert ((Get-Content -LiteralPath $log).Count -eq 1) 'Fastfetch was launched during animation'
Write-Output $report.Trim()
Write-Output "Windows process checks: $checks passed"
