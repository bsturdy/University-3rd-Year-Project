param(
    [int]$AdsPort = 851,
    [int]$HttpPort = 8080,
    [switch]$Demo,
    [switch]$AddStartup,
    [switch]$AddFirewallRule
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$VenvDir = Join-Path $ScriptDir ".venv"
$PythonExe = Join-Path $VenvDir "Scripts\python.exe"
$ConfigPath = Join-Path $ScriptDir "config.json"
$StartBatPath = Join-Path $ScriptDir "start_web_hmi.bat"
$PackagesDir = Join-Path $ScriptDir "packages"

function Write-Step {
    param([string]$Message)
    Write-Host ""
    Write-Host "== $Message ==" -ForegroundColor Cyan
}

Write-Step "Preparing ESP Mesh HMI installation"
Write-Host "Install folder: $ScriptDir"

Write-Step "Checking Python"
$Python = Get-Command python -ErrorAction SilentlyContinue
if (-not $Python) {
    throw "Python is not on PATH. Install Python x64, tick 'Add python.exe to PATH', reopen this window, then rerun."
}
python --version

Write-Step "Creating virtual environment"
if (-not (Test-Path $PythonExe)) {
    python -m venv $VenvDir
}
if (-not (Test-Path $PythonExe)) {
    throw "Virtual environment creation failed. Expected: $PythonExe"
}

Write-Step "Installing Python dependencies"
& $PythonExe -m pip install --upgrade pip
if ((Test-Path $PackagesDir) -and ((Get-ChildItem -Path $PackagesDir -File -ErrorAction SilentlyContinue).Count -gt 0)) {
    & $PythonExe -m pip install --no-index --find-links $PackagesDir -r (Join-Path $ScriptDir "requirements.txt")
} else {
    & $PythonExe -m pip install -r (Join-Path $ScriptDir "requirements.txt")
}

Write-Step "Writing config.json"
$config = [ordered]@{
    ams_net_id = "local"
    ads_port = $AdsPort
    http_host = "0.0.0.0"
    http_port = $HttpPort
    refresh_ms = 1000
    demo = [bool]$Demo
}
[System.IO.File]::WriteAllText($ConfigPath, ($config | ConvertTo-Json -Depth 4), [System.Text.UTF8Encoding]::new($false))
Write-Host "Written: $ConfigPath"

Write-Step "Writing startup batch file"
@"
@echo off
cd /d "%~dp0"
".venv\Scripts\python.exe" web_hmi.py
"@ | Set-Content -Path $StartBatPath -Encoding ASCII
Write-Host "Written: $StartBatPath"

if ($AddFirewallRule) {
    Write-Step "Adding Windows Firewall rule"
    $ruleName = "ESP Mesh HMI TCP $HttpPort"
    try {
        $existing = Get-NetFirewallRule -DisplayName $ruleName -ErrorAction SilentlyContinue
        if ($existing) {
            Write-Host "Firewall rule already exists: $ruleName"
        } else {
            New-NetFirewallRule -DisplayName $ruleName -Direction Inbound -Action Allow -Protocol TCP -LocalPort $HttpPort | Out-Null
            Write-Host "Added firewall rule: $ruleName"
        }
    } catch {
        Write-Host "Could not add firewall rule. Run as Administrator if LAN clients cannot connect." -ForegroundColor Yellow
        Write-Host $_.Exception.Message -ForegroundColor Yellow
    }
}

if ($AddStartup) {
    Write-Step "Configuring startup shortcut"
    $startupDir = [Environment]::GetFolderPath("Startup")
    $shortcutPath = Join-Path $startupDir "ESP Mesh HMI.lnk"
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($shortcutPath)
    $shortcut.TargetPath = $StartBatPath
    $shortcut.WorkingDirectory = $ScriptDir
    $shortcut.Description = "Start ESP Mesh HMI"
    $shortcut.Save()
    Write-Host "Created startup shortcut: $shortcutPath"
}

Write-Step "Smoke test"
& $PythonExe -m py_compile (Join-Path $ScriptDir "ads_snapshot.py") (Join-Path $ScriptDir "web_hmi.py")
if ($LASTEXITCODE -ne 0) {
    throw "Python compile smoke test failed."
}
Write-Host "Python files compile successfully."

Write-Host ""
Write-Host "Installation complete." -ForegroundColor Green
Write-Host "Run now: $StartBatPath"
Write-Host "Local page: http://127.0.0.1:$HttpPort"
Write-Host "LAN page: http://the-controller-lan-address:$HttpPort"
