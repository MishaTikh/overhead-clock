# Overhead one-click installer for Windows.
#
# Finds an Ulanzi TC001 on your Wi-Fi and installs or updates the Overhead firmware over Wi-Fi:
#   - a clock still on Ulanzi's stock firmware: through its built-in update page (no password)
#   - a clock already running Overhead: through Overhead's update page (asks for the admin password)
# USB isn't used: flashing the TC001 over USB doesn't work reliably from Windows.
#
# Usage: double-click Install-Overhead.bat, or drop a different .bin onto it.
param(
    [string]$Firmware = "",
    [string]$Clock = ""
)

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$curl = Join-Path $env:SystemRoot "System32\curl.exe"   # built into Windows 10 and 11

function Say($text, $color = "Gray") { Write-Host $text -ForegroundColor $color }
function Fail($text) {
    Say ""
    Say $text "Red"
    Say ""
    Read-Host "Press Enter to close"
    exit 1
}

Say ""
Say "  OVERHEAD  installer" "Yellow"
Say "  Shows where the plane over your house came from, only while you can hear it." "DarkGray"
Say ""

if (-not (Test-Path $curl)) { Fail "This needs Windows 10 or 11 (curl.exe wasn't found)." }

# ---- 1. the firmware file
if (-not $Firmware) {
    $candidates = @(
        (Join-Path $here "overhead-tc001-app.bin"),
        (Join-Path $here "..\firmware\release\overhead-tc001-app.bin")
    )
    $Firmware = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $Firmware -or -not (Test-Path $Firmware)) {
    Fail "Couldn't find overhead-tc001-app.bin. Put it next to this installer (it's in the project's releases), or drop it onto Install-Overhead.bat."
}
$Firmware = (Resolve-Path $Firmware).Path
$bytes = [System.IO.File]::ReadAllBytes($Firmware)
if ($bytes.Length -lt 100000 -or $bytes[0] -ne 0xE9) { Fail "$Firmware doesn't look like an ESP32 firmware image." }
Say ("Firmware: {0}  ({1:N1} MB)" -f (Split-Path -Leaf $Firmware), ($bytes.Length / 1MB))

# ---- 2. find the clock
function Probe($target) {
    # returns "stock", "overhead" or $null
    $id = & $curl -s -m 4 "http://$target/update/identity" 2>$null
    if ($LASTEXITCODE -eq 0 -and $id -match '"hardware"') { return "stock" }
    $hdr = & $curl -s -m 4 -D - -o NUL "http://$target/" 2>$null
    if ($LASTEXITCODE -eq 0 -and ($hdr -join "`n") -match 'realm="Overhead Clock"') { return "overhead" }
    return $null
}

$kind = $null
if ($Clock) {
    $kind = Probe $Clock
} else {
    Say "Looking for the clock on your Wi-Fi..."
    foreach ($name in @("overhead.local", "clock.local", "192.168.4.1")) {
        $kind = Probe $name
        if ($kind) { $Clock = $name; break }
    }
}
while (-not $kind) {
    Say ""
    Say "Couldn't find the clock automatically." "Yellow"
    Say "  - Is it on and connected to your Wi-Fi? A clock running Overhead shows its address when you hold the middle button."
    Say "  - A new clock: join its Wi-Fi on your phone, open 192.168.4.1 and connect it to your home Wi-Fi first."
    $Clock = Read-Host "Type the clock's address (like 192.168.1.42), or press Enter to quit"
    if (-not $Clock) { exit 1 }
    $Clock = $Clock.Trim() -replace '^https?://', '' -replace '/.*$', ''
    $kind = Probe $Clock
}

# use the IP address from here on (Overhead only answers requests addressed to its own name or IP)
try {
    if ($Clock -notmatch '^\d+\.\d+\.\d+\.\d+$') {
        $ip = (Resolve-DnsName $Clock -Type A -ErrorAction Stop | Where-Object { $_.IPAddress } | Select-Object -First 1).IPAddress
        if ($ip) { $Clock = $ip }
    }
} catch { }

if ($kind -eq "stock") {
    Say "Found a TC001 running Ulanzi's firmware at $Clock." "Green"
    Say "This replaces Ulanzi's firmware with Overhead. You can install Ulanzi's or other firmware again later from Overhead's settings page."
} else {
    Say "Found a clock running Overhead at $Clock." "Green"
}
Say ""
$go = Read-Host "Install now? (Y/n)"
if ($go -and $go -notmatch '^[Yy]') { exit 0 }

# ---- 3. upload
$tmp = New-TemporaryFile
try {
    if ($kind -eq "stock") {
        $md5 = (Get-FileHash -Algorithm MD5 $Firmware).Hash.ToLower()
        Say "Uploading to the stock update page (about 30 seconds)..."
        $code = & $curl -s -m 300 -o $tmp -w "%{http_code}" -F "MD5=$md5" -F "firmware=@$Firmware;filename=firmware;type=application/octet-stream" "http://$Clock/update"
    } else {
        Say "Sign in with the clock's admin password. Hold the clock's middle button to see it, four characters at a time"
        Say "(the left and right buttons step back and forward)."
        $secure = Read-Host "Admin password" -AsSecureString
        $plain = [Runtime.InteropServices.Marshal]::PtrToStringBSTR([Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure))
        Say "Uploading to Overhead's update page (about 30 seconds)..."
        # the password goes to curl on stdin, not the command line
        $config = "user = `"admin:$($plain.Replace('\', '\\').Replace('"', '\"'))`""
        $code = $config | & $curl -s -m 300 -K - --digest -o $tmp -w "%{http_code}" `
            -H "X-Overhead: 1" -H "Content-Type: application/octet-stream" --data-binary "@$Firmware" "http://$Clock/api/update"
        $plain = $null
    }
    $body = Get-Content $tmp -Raw -ErrorAction SilentlyContinue
} finally {
    Remove-Item $tmp -ErrorAction SilentlyContinue
}

if ($code -eq "200") {
    Say ""
    Say "Done. The clock is restarting into Overhead." "Green"
    if ($kind -eq "stock") {
        Say "Next: hold the clock's middle button to see its settings address and password, open that address on"
        Say "your phone, sign in as admin, and enter your street address."
    }
} elseif ($code -eq "401") {
    Fail "The password wasn't accepted. Hold the middle button to see it again (12 characters, no spaces)."
} elseif ($code -eq "429") {
    Fail "Too many wrong passwords from this computer. Wait a minute and try again."
} else {
    $msg = ""
    if ($body -and $body.Length -lt 400) { $msg = $body.Trim() }
    Fail "The upload didn't work (HTTP $code). $msg The clock keeps its current firmware; try again, or open its update page in a browser."
}
Say ""
Read-Host "Press Enter to close"
