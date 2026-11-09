# SPDX-License-Identifier: AGPL-3.0-only
param([string]$WslDistribution = 'Ubuntu-24.04')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$savedPath = $env:PATH
Push-Location -LiteralPath $repoRoot
try {
    foreach ($relative in @('.work/tools/w64devkit-2.11.0/w64devkit/bin', '.work/tools/dtc-1.7.0')) {
        $bin = Join-Path $repoRoot $relative
        if (Test-Path -LiteralPath $bin -PathType Container) { $env:PATH = "$bin;$env:PATH" }
    }
    foreach ($name in @('python', 'cc', 'dtc', 'fdtget', 'fdtput', 'fdtoverlay', 'wsl')) {
        if (-not (Get-Command $name -CommandType Application -ErrorAction SilentlyContinue)) {
            throw "Missing $name; see docs/HOST_TOOLS.md"
        }
    }
    python -X utf8 tools/check_repo.py
    if ($LASTEXITCODE -ne 0) { throw 'Repository checks failed' }
    python -X utf8 -m unittest discover -s tests -v
    if ($LASTEXITCODE -ne 0) { throw 'Native Windows regressions failed' }
    # Linux syscalls/PTY/FIFO and POSIX permission fixtures run in WSL.
    wsl --distribution $WslDistribution --cd $repoRoot --exec python3 -c 'import shutil; names=("bash","gcc","cc","dtc","fdtget","fdtput","fdtoverlay","shellcheck"); missing=[n for n in names if not shutil.which(n)]; print("WSL missing tools:", missing); raise SystemExit(bool(missing))'
    if ($LASTEXITCODE -ne 0) { throw 'WSL dependencies are incomplete' }
    wsl --distribution $WslDistribution --cd $repoRoot --exec python3 -X utf8 -m unittest discover -s tests -v
    if ($LASTEXITCODE -ne 0) { throw 'WSL regressions failed' }
    wsl --distribution $WslDistribution --cd $repoRoot --exec shellcheck --severity=error tools/build_kernel_l2.sh tools/build_kernel_l2_macos.sh
    if ($LASTEXITCODE -ne 0) { throw 'ShellCheck failed' }
} finally {
    $env:PATH = $savedPath
    Pop-Location
}
