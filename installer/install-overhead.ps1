# Overhead installer for Windows: a small window that finds your Ulanzi TC001 on the Wi-Fi, shows what the clock
# runs and what the firmware file contains, installs over Wi-Fi, and confirms the clock came back on the new version.
#   - a clock still on Ulanzi's stock firmware: through its built-in update page (no password)
#   - a clock already running Overhead: through Overhead's update page (asks for the admin password)
# USB isn't used: flashing the TC001 over USB doesn't work reliably from Windows.
#
# Usage: double-click Install-Overhead.bat, or drop a different .bin onto it.
#        install-overhead.ps1 -NoGui        prints what it finds and exits (for testing)
param(
    [string]$Firmware = "",
    [string]$Clock = "",
    [switch]$NoGui,
    [string]$Screenshot = ""   # render the window with sample state to this .png and exit (for testing)
)

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$curl = Join-Path $env:SystemRoot "System32\curl.exe"   # built into Windows 10 and 11

# ---------------------------------------------------------------- helpers (no UI)
function Get-FirmwareInfo([string]$path) {
    # The image carries its version in the clock's user-agent string: "overhead-clock/0.2.0 (+https://...)".
    $bytes = [System.IO.File]::ReadAllBytes($path)
    $info = @{ Path = $path; Name = (Split-Path -Leaf $path); Bytes = $bytes.Length; Valid = $false; Version = $null; Sha256 = $null }
    if ($bytes.Length -lt 100000 -or $bytes[0] -ne 0xE9) { return $info }
    $info.Valid = $true
    $text = [System.Text.Encoding]::GetEncoding(28591).GetString($bytes)
    $m = [regex]::Match($text, "overhead-clock/([0-9]+\.[0-9]+\.[0-9]+) \(\+")
    if ($m.Success) { $info.Version = $m.Groups[1].Value }
    $info.Sha256 = (Get-FileHash -Algorithm SHA256 $path).Hash.ToLower()
    return $info
}

function Find-DefaultFirmware {
    foreach ($c in @((Join-Path $here "overhead-tc001-app.bin"), (Join-Path $here "..\firmware\release\overhead-tc001-app.bin"))) {
        if (Test-Path $c) { return (Resolve-Path $c).Path }
    }
    return $null
}

function Quote-Arg([string]$a) { '"' + ($a -replace '(\\*)"', '$1$1\"') + '"' }

# Runs curl without blocking the window. Returns the Process; read .StandardOutput once it has exited.
function Start-Curl([string[]]$arguments, [string]$stdin = $null) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $curl
    $psi.Arguments = ($arguments | ForEach-Object { Quote-Arg $_ }) -join " "
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardInput = $true
    $psi.CreateNoWindow = $true
    $p = [System.Diagnostics.Process]::Start($psi)
    if ($stdin) { $p.StandardInput.Write($stdin) }
    $p.StandardInput.Close()
    return $p
}

# Blocking version, for -NoGui.
function Invoke-Curl([string[]]$arguments, [string]$stdin = $null) {
    $p = Start-Curl $arguments $stdin
    $out = $p.StandardOutput.ReadToEnd()
    $p.WaitForExit()
    return @{ Code = $p.ExitCode; Out = $out }
}

function Probe-Clock([string]$target) {
    # returns @{ Kind = "stock" | "overhead" | $null; Identity = <string> }
    $r = Invoke-Curl @("-s", "-m", "4", "http://$target/update/identity")
    if ($r.Code -eq 0 -and $r.Out -match '"hardware"') { return @{ Kind = "stock"; Identity = $r.Out } }
    $r = Invoke-Curl @("-s", "-m", "4", "-D", "-", "-o", "NUL", "http://$target/")
    if ($r.Code -eq 0 -and $r.Out -match 'realm="Overhead Clock"') { return @{ Kind = "overhead"; Identity = "" } }
    return @{ Kind = $null; Identity = "" }
}

function Resolve-ClockIp([string]$name) {
    # Overhead only answers requests addressed to its own name or IP; the IP is the safe choice
    if ($name -match '^\d+\.\d+\.\d+\.\d+$') { return $name }
    try {  # Windows' own resolver, which also answers .local names (mDNS)
        $ip = [System.Net.Dns]::GetHostAddresses($name) | Where-Object { $_.AddressFamily -eq "InterNetwork" } | Select-Object -First 1
        if ($ip) { return $ip.IPAddressToString }
    } catch { }
    return $name
}

function Curl-Config([string]$password) {
    # the password goes to curl on stdin as a config line, never on the command line
    return "user = `"admin:$($password.Replace('\', '\\').Replace('"', '\"'))`"`n"
}

function Describe-Stock([string]$identity) {
    try {
        $j = $identity | ConvertFrom-Json
        $parts = @()
        foreach ($k in @("id", "hardware", "version")) { if ($j.$k) { $parts += "$k $($j.$k)" } }
        if ($parts.Count) { return "Ulanzi firmware (" + ($parts -join ", ") + ")" }
    } catch { }
    return "Ulanzi firmware"
}

function Format-Uptime([int]$s) {
    if ($s -lt 3600) { return "$([math]::Round($s / 60)) min" }
    if ($s -lt 86400) { return "$([math]::Round($s / 3600, 1)) h" }
    return "$([math]::Round($s / 86400, 1)) days"
}

# ---------------------------------------------------------------- headless mode
if ($NoGui) {
    if (-not $Firmware) { $Firmware = Find-DefaultFirmware }
    if ($Firmware) {
        $fi = Get-FirmwareInfo $Firmware
        "File: $($fi.Name)  valid=$($fi.Valid)  version=$($fi.Version)  size=$([math]::Round($fi.Bytes / 1MB, 2)) MB  sha256=$($fi.Sha256)"
    } else { "File: none found" }
    $targets = if ($Clock) { @($Clock) } else { @("overhead.local", "clock.local", "192.168.4.1") }
    foreach ($t in $targets) {
        $pr = Probe-Clock $t
        if ($pr.Kind) { "Clock: $($pr.Kind) at $t ($(Resolve-ClockIp $t)) $(if ($pr.Kind -eq 'stock') { Describe-Stock $pr.Identity })"; exit 0 }
    }
    "Clock: not found"
    exit 0
}

# ---------------------------------------------------------------- the window
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
try {
    Add-Type -Namespace Win32 -Name Dpi -MemberDefinition '[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();'
    [void][Win32.Dpi]::SetProcessDPIAware()
} catch { }
[System.Windows.Forms.Application]::EnableVisualStyles()

$font = New-Object System.Drawing.Font("Segoe UI", 9.75)
$bold = New-Object System.Drawing.Font("Segoe UI", 9.75, [System.Drawing.FontStyle]::Bold)
$mono = New-Object System.Drawing.Font("Consolas", 9)

$form = New-Object System.Windows.Forms.Form
$form.Text = "Overhead installer"
$form.Font = $font
$form.AutoScaleMode = "Dpi"
$form.StartPosition = "CenterScreen"
$form.FormBorderStyle = "FixedSingle"
$form.MaximizeBox = $false
$form.ClientSize = New-Object System.Drawing.Size(560, 620)

function New-Label([string]$text, [int]$x, [int]$y, [int]$w, [int]$h = 22, $f = $null) {
    $l = New-Object System.Windows.Forms.Label
    $l.Text = $text; $l.Left = $x; $l.Top = $y; $l.Width = $w; $l.Height = $h
    if ($f) { $l.Font = $f }
    $form.Controls.Add($l); return $l
}
function New-Button([string]$text, [int]$x, [int]$y, [int]$w, [int]$h = 30) {
    $b = New-Object System.Windows.Forms.Button
    $b.Text = $text; $b.Left = $x; $b.Top = $y; $b.Width = $w; $b.Height = $h
    $form.Controls.Add($b); return $b
}

$y = 14
[void](New-Label "OVERHEAD" 20 $y 200 24 $bold)
[void](New-Label "Where the plane over your house came from, while you can hear it." 20 ($y + 24) 520 20)

# -- clock
$y = 72
[void](New-Label "Clock" 20 $y 100 22 $bold)
$clockStatus = New-Label "Looking for the clock on your Wi-Fi..." 20 ($y + 24) 520 44
$clockBox = New-Object System.Windows.Forms.TextBox
$clockBox.Left = 20; $clockBox.Top = $y + 72; $clockBox.Width = 300; $clockBox.Font = $mono
$clockBox.Text = $Clock
$form.Controls.Add($clockBox)
$findBtn = New-Button "Find" 330 ($y + 70) 90
$hintClock = New-Label "If it isn't found: type its address or name (like 192.168.1.42) and press Find." 20 ($y + 104) 520 20
$hintClock.ForeColor = [System.Drawing.Color]::Gray

# -- firmware file
$y = 208
[void](New-Label "Firmware file" 20 $y 200 22 $bold)
$fileStatus = New-Label "" 20 ($y + 24) 420 44
$chooseBtn = New-Button "Choose..." 450 ($y + 24) 90

# -- password
$y = 292
[void](New-Label "Admin password" 20 $y 200 22 $bold)
$pwBox = New-Object System.Windows.Forms.TextBox
$pwBox.Left = 20; $pwBox.Top = $y + 26; $pwBox.Width = 300; $pwBox.UseSystemPasswordChar = $true; $pwBox.Font = $mono
$form.Controls.Add($pwBox)
$hintPw = New-Label "Only for a clock already running Overhead. Hold the clock's middle button for 3 seconds to see it, four characters at a time." 20 ($y + 56) 520 40
$hintPw.ForeColor = [System.Drawing.Color]::Gray

# -- install
$y = 400
$installBtn = New-Button "Install" 20 $y 140 36
$installBtn.Font = $bold
$progress = New-Object System.Windows.Forms.ProgressBar
$progress.Left = 176; $progress.Top = $y + 6; $progress.Width = 364; $progress.Height = 24
$form.Controls.Add($progress)

$log = New-Object System.Windows.Forms.TextBox
$log.Left = 20; $log.Top = $y + 50; $log.Width = 520; $log.Height = 150
$log.Multiline = $true; $log.ReadOnly = $true; $log.ScrollBars = "Vertical"; $log.Font = $mono
$log.BackColor = [System.Drawing.Color]::White
$form.Controls.Add($log)

$link = New-Object System.Windows.Forms.LinkLabel
$link.Text = "github.com/MishaTikh/overhead-clock/releases"; $link.Left = 20; $link.Top = 596; $link.Width = 400
$link.add_LinkClicked({ Start-Process "https://github.com/MishaTikh/overhead-clock/releases" })
$form.Controls.Add($link)

function Log([string]$line) {
    $log.AppendText((Get-Date -Format "HH:mm:ss") + "  " + $line + "`r`n")
}

# ---------------------------------------------------------------- state
$script:found = @{ Kind = $null; Host = $null; Ip = $null; Identity = ""; Version = $null }
$script:fw = $null
$script:busy = $false

function Set-Busy([bool]$b) {
    $script:busy = $b
    $installBtn.Enabled = -not $b
    $findBtn.Enabled = -not $b
    $chooseBtn.Enabled = -not $b
    $clockBox.Enabled = -not $b
    $progress.Style = if ($b) { "Marquee" } else { "Blocks" }
    if (-not $b) { $progress.Value = 0 }
}

function Refresh-Buttons {
    $ok = $script:found.Kind -and $script:fw -and $script:fw.Valid
    $installBtn.Enabled = [bool]$ok -and -not $script:busy
    $pwBox.Enabled = $script:found.Kind -eq "overhead"
    if ($script:found.Kind -eq "stock") { $pwBox.Text = "" }
}

function Show-File([string]$path) {
    if (-not $path -or -not (Test-Path $path)) {
        $script:fw = $null
        $fileStatus.Text = "No firmware file. Put overhead-tc001-app.bin next to the installer, or choose it."
        $fileStatus.ForeColor = [System.Drawing.Color]::Firebrick
        Refresh-Buttons; return
    }
    $fi = Get-FirmwareInfo $path
    $script:fw = $fi
    if (-not $fi.Valid) {
        $fileStatus.Text = "$($fi.Name) doesn't look like an ESP32 firmware image."
        $fileStatus.ForeColor = [System.Drawing.Color]::Firebrick
    } else {
        $ver = if ($fi.Version) { "Overhead $($fi.Version)" } else { "unknown version (not an Overhead image?)" }
        $fileStatus.Text = "$($fi.Name): $ver, $([math]::Round($fi.Bytes / 1MB, 1)) MB`r`nSHA-256 $($fi.Sha256.Substring(0, 16))..."
        $fileStatus.ForeColor = [System.Drawing.SystemColors]::ControlText
    }
    Refresh-Buttons
}

function Show-Clock {
    $c = $script:found
    if (-not $c.Kind) {
        $clockStatus.Text = "Couldn't find the clock. Is it on and connected to your Wi-Fi? Type its address below and press Find."
        $clockStatus.ForeColor = [System.Drawing.Color]::Firebrick
    } elseif ($c.Kind -eq "stock") {
        $clockStatus.Text = "Found a TC001 at $($c.Ip) running $(Describe-Stock $c.Identity).`r`nInstalling replaces Ulanzi's firmware with Overhead; no password needed."
        $clockStatus.ForeColor = [System.Drawing.Color]::DarkGreen
    } else {
        $v = if ($c.Version) { " $($c.Version)" } else { "" }
        $clockStatus.Text = "Found a clock running Overhead$v at $($c.Ip)."
        $clockStatus.ForeColor = [System.Drawing.Color]::DarkGreen
    }
    Refresh-Buttons
}

# Background steps: one curl process at a time with a continuation, driven by a timer so the window stays alive.
$script:pending = $null   # @{ Proc; Then = scriptblock(code, out) }
$script:later = $null     # @{ At = DateTime; Then = scriptblock }
$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 100
$timer.add_Tick({
    $p = $script:pending
    if ($p -and $p.Proc.HasExited) {
        $script:pending = $null
        $out = $p.Proc.StandardOutput.ReadToEnd()
        $code = $p.Proc.ExitCode
        & $p.Then $code $out
    }
    $l = $script:later
    if ($l -and (Get-Date) -ge $l.At) {
        $script:later = $null
        & $l.Then
    }
})
$timer.Start()
function Then([string[]]$arguments, [string]$stdin, [scriptblock]$then) {
    $script:pending = @{ Proc = (Start-Curl $arguments $stdin); Then = $then }
}
function Later([int]$ms, [scriptblock]$then) {  # a pause that keeps the window responsive
    $script:later = @{ At = (Get-Date).AddMilliseconds($ms); Then = $then }
}

# ---- find the clock: probe the candidates one after another
function Find-Clock([string[]]$targets, [int]$i = 0) {
    if ($i -ge $targets.Count) {
        $script:found = @{ Kind = $null; Host = $null; Ip = $null; Identity = ""; Version = $null }
        Set-Busy $false; Show-Clock; Log "No clock found at $($targets -join ', ')."
        return
    }
    $t = $targets[$i]
    $clockStatus.Text = "Looking for the clock at $t..."
    $clockStatus.ForeColor = [System.Drawing.SystemColors]::ControlText
    Then @("-s", "-m", "4", "http://$t/update/identity") $null {
        param($code, $out)
        if ($code -eq 0 -and $out -match '"hardware"') {
            $script:found = @{ Kind = "stock"; Host = $t; Ip = (Resolve-ClockIp $t); Identity = $out; Version = $null }
            Set-Busy $false; Show-Clock; Log "Found Ulanzi's firmware at $t."
            return
        }
        Then @("-s", "-m", "4", "-D", "-", "-o", "NUL", "http://$t/") $null {
            param($code2, $out2)
            if ($code2 -eq 0 -and $out2 -match 'realm="Overhead Clock"') {
                $script:found = @{ Kind = "overhead"; Host = $t; Ip = (Resolve-ClockIp $t); Identity = ""; Version = $null }
                Set-Busy $false; Show-Clock; Log "Found Overhead at $t ($($script:found.Ip)). Type the admin password to see its version and install."
                return
            }
            Find-Clock $targets ($i + 1)
        }.GetNewClosure()
    }.GetNewClosure()
}

$findBtn.add_Click({
    $typed = $clockBox.Text.Trim() -replace '^https?://', '' -replace '/.*$', ''
    $targets = if ($typed) { @($typed) } else { @("overhead.local", "clock.local", "192.168.4.1") }
    Set-Busy $true
    Find-Clock $targets
})

$chooseBtn.add_Click({
    $dlg = New-Object System.Windows.Forms.OpenFileDialog
    $dlg.Filter = "Firmware image (*.bin)|*.bin|All files (*.*)|*.*"
    $dlg.InitialDirectory = $here
    if ($dlg.ShowDialog($form) -eq "OK") { Show-File $dlg.FileName }
})

# ---- install
function Finish-Install([string]$msg, [bool]$ok) {
    Set-Busy $false
    $progress.Value = if ($ok) { 100 } else { 0 }
    Log $msg
    $icon = if ($ok) { "Information" } else { "Warning" }
    [System.Windows.Forms.MessageBox]::Show($form, $msg, "Overhead installer", "OK", $icon) | Out-Null
    Refresh-Buttons
}

# Waits for the clock to come back after an update, then reports what it runs. Up to about 75 seconds.
function Wait-ForClock([string]$config, [int]$tries = 0) {
    $ip = $script:found.Ip
    if ($tries -ge 25) { Finish-Install "The clock hasn't answered yet. Give it a minute; if it doesn't come back, unplug it and plug it in again." $false; return }
    $progress.Style = "Blocks"; $progress.Value = [math]::Min(99, 60 + $tries * 2)
    $tmp = [System.IO.Path]::GetTempFileName()
    if ($config) {
        Then @("-s", "-m", "3", "-K", "-", "--digest", "-o", $tmp, "-w", "%{http_code}", "http://$ip/api/status") $config {
            param($code, $out)
            $body = ""; try { $body = Get-Content $tmp -Raw -ErrorAction SilentlyContinue } catch { }
            Remove-Item $tmp -ErrorAction SilentlyContinue
            if ($out -eq "200" -and $body) {
                try { $j = $body | ConvertFrom-Json } catch { $j = $null }
                $v = if ($j) { $j.version } else { "?" }
                $script:found.Version = $v
                Show-Clock
                if ($script:fw.Version -and $v -ne $script:fw.Version) {
                    Finish-Install "The clock is back but reports $v, not $($script:fw.Version). Try the update again." $false
                } else {
                    Finish-Install "Done. The clock is back and running Overhead $v (restart reason: $($j.reset_reason))." $true
                }
                return
            }
            Later 2500 { Wait-ForClock $config ($tries + 1) }.GetNewClosure()
        }.GetNewClosure()
    } else {
        Then @("-s", "-m", "3", "-D", "-", "-o", "NUL", "http://$ip/") $null {
            param($code, $out)
            Remove-Item $tmp -ErrorAction SilentlyContinue
            if ($code -eq 0 -and $out -match 'realm="Overhead Clock"') {
                $script:found.Kind = "overhead"; Show-Clock
                Finish-Install "Done. Overhead is running at $ip.`n`nNext: hold the clock's middle button for 3 seconds to see its settings address and password, open that address on your phone, sign in as admin, and enter your street address." $true
                return
            }
            Later 2500 { Wait-ForClock $null ($tries + 1) }.GetNewClosure()
        }.GetNewClosure()
    }
}

function Upload-Overhead([string]$config) {
    $fw = $script:fw; $ip = $script:found.Ip
    Log "Uploading $($fw.Name) to Overhead's update page (about 30 seconds)..."
    $progress.Style = "Marquee"
    $tmp = [System.IO.Path]::GetTempFileName()
    # the image's SHA-256 rides in the URL, which the Digest sign-in signs; the clock refuses an image that doesn't match it
    Then @("-s", "-m", "300", "-K", "-", "--digest", "-o", $tmp, "-w", "%{http_code}", "-H", "X-Overhead: 1",
           "-H", "Content-Type: application/octet-stream", "--data-binary", "@$($fw.Path)", "http://$ip/api/update?sha256=$($fw.Sha256)") $config {
        param($code, $out)
        $body = ""; try { $body = Get-Content $tmp -Raw -ErrorAction SilentlyContinue } catch { }
        Remove-Item $tmp -ErrorAction SilentlyContinue
        if ($out -eq "200") {
            Log "Upload accepted. The clock is verifying the image and restarting..."
            Later 4000 { Wait-ForClock $config }.GetNewClosure()
        } elseif ($out -eq "401") { Finish-Install "The password wasn't accepted. Hold the middle button to see it again (12 characters, no spaces)." $false }
        elseif ($out -eq "429") { Finish-Install "Too many wrong passwords from this computer. Wait a minute and try again." $false }
        else {
            $msg = ""; try { $msg = ($body | ConvertFrom-Json).error } catch { if ($body -and $body.Length -lt 300) { $msg = $body.Trim() } }
            Finish-Install "The upload didn't work (HTTP $out). $msg`nThe clock keeps its current firmware." $false
        }
    }.GetNewClosure()
}

function Upload-Stock {
    $fw = $script:fw; $ip = $script:found.Ip
    $md5 = (Get-FileHash -Algorithm MD5 $fw.Path).Hash.ToLower()
    Log "Uploading $($fw.Name) to Ulanzi's update page (about 30 seconds)..."
    $progress.Style = "Marquee"
    $tmp = [System.IO.Path]::GetTempFileName()
    Then @("-s", "-m", "300", "-o", $tmp, "-w", "%{http_code}", "-F", "MD5=$md5",
           "-F", "firmware=@$($fw.Path);filename=firmware;type=application/octet-stream", "http://$ip/update") $null {
        param($code, $out)
        Remove-Item $tmp -ErrorAction SilentlyContinue
        if ($out -eq "200") {
            Log "Upload accepted. The clock is restarting into Overhead..."
            Later 4000 { Wait-ForClock $null }
        } else {
            Finish-Install "The upload didn't work (HTTP $out). The clock keeps its current firmware; try again, or open http://$ip/update in a browser." $false
        }
    }.GetNewClosure()
}

$installBtn.add_Click({
    $fw = $script:fw; $c = $script:found
    if (-not $fw -or -not $fw.Valid -or -not $c.Kind) { return }
    Set-Busy $true
    $log.Clear()
    Log "File: $($fw.Name), Overhead $($fw.Version), SHA-256 $($fw.Sha256)"
    if ($c.Kind -eq "stock") {
        Log "Clock: $(Describe-Stock $c.Identity) at $($c.Ip)"
        Upload-Stock
        return
    }
    $pw = $pwBox.Text
    if ($pw.Length -lt 8) {
        Set-Busy $false; Refresh-Buttons
        [System.Windows.Forms.MessageBox]::Show($form, "Type the clock's admin password first (12 characters, shown when you hold the middle button).", "Overhead installer", "OK", "Information") | Out-Null
        return
    }
    $config = Curl-Config $pw
    # first: sign in and read what the clock runs, so you see old -> new before anything changes
    Log "Signing in to the clock at $($c.Ip)..."
    $tmp = [System.IO.Path]::GetTempFileName()
    Then @("-s", "-m", "8", "-K", "-", "--digest", "-o", $tmp, "-w", "%{http_code}", "http://$($c.Ip)/api/status") $config {
        param($code, $out)
        $body = ""; try { $body = Get-Content $tmp -Raw -ErrorAction SilentlyContinue } catch { }
        Remove-Item $tmp -ErrorAction SilentlyContinue
        if ($out -eq "401") { Finish-Install "The password wasn't accepted. Hold the middle button to see it again (12 characters, no spaces)." $false; return }
        if ($out -eq "429") { Finish-Install "Too many wrong passwords from this computer. Wait a minute and try again." $false; return }
        if ($out -ne "200") { Finish-Install "The clock answered HTTP $out. Check the address and try again." $false; return }
        try { $j = $body | ConvertFrom-Json } catch { $j = $null }
        $v = if ($j) { $j.version } else { "?" }
        $script:found.Version = $v
        Show-Clock
        $up = if ($j -and $j.uptime_s) { ", running for " + (Format-Uptime $j.uptime_s) } else { "" }
        Log "Clock: Overhead $v$up, last restart: $($j.reset_reason), free memory $([math]::Round($j.free_heap / 1024)) KB"
        $new = $script:fw.Version
        $question = $null
        if ($new -and $v -eq $new) { $question = "The clock already runs Overhead $v. Install it again?" }
        elseif ($new -and $v -and ([version]$new -lt [version]$v)) { $question = "This file is OLDER than what the clock runs ($v -> $new). Install the older version?" }
        elseif ($new) { Log "Update: Overhead $v -> $new" }
        if ($question) {
            $a = [System.Windows.Forms.MessageBox]::Show($form, $question, "Overhead installer", "YesNo", "Question")
            if ($a -ne "Yes") { Finish-Install "Nothing changed." $false; return }
        }
        Upload-Overhead $config
    }.GetNewClosure()
})

# ---------------------------------------------------------------- start
if (-not (Test-Path $curl)) {
    [System.Windows.Forms.MessageBox]::Show("This needs Windows 10 or 11 (curl.exe wasn't found).", "Overhead installer", "OK", "Error") | Out-Null
    exit 1
}
if (-not $Firmware) { $Firmware = Find-DefaultFirmware }
Show-File $Firmware
if ($Screenshot) {
    $form.Show()
    [System.Windows.Forms.Application]::DoEvents()
    $script:found = @{ Kind = "overhead"; Host = "overhead.local"; Ip = "192.168.1.42"; Identity = ""; Version = "0.1.8" }
    Set-Busy $false; Show-Clock
    Log "Found Overhead at overhead.local (192.168.1.42). Type the admin password to see its version and install."
    Log "Clock: Overhead 0.1.8, running for 6.2 days, last restart: power on, free memory 139 KB"
    Log "Update: Overhead 0.1.8 -> 0.2.0"
    "state: kind=$($script:found.Kind) status=$($clockStatus.Text)"
    $bmp = New-Object System.Drawing.Bitmap $form.Width, $form.Height
    $form.DrawToBitmap($bmp, (New-Object System.Drawing.Rectangle 0, 0, $form.Width, $form.Height))
    $bmp.Save($Screenshot)
    exit 0
}
$form.add_Shown({
    Set-Busy $true
    $targets = if ($Clock) { @($Clock) } else { @("overhead.local", "clock.local", "192.168.4.1") }
    Find-Clock $targets
})
[void]$form.ShowDialog()
