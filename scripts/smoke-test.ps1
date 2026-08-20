<#
.SYNOPSIS
    iPhone-less smoke test for the Windows AirPlay receiver (docs/SPEC.md, section 2 item 5).

.DESCRIPTION
    Verifies everything that can be verified without an actual iOS device:
      a. MSYS2 UCRT64 toolchain is installed
      b. every GStreamer element UxPlay needs is present
      c. build\uxplay.exe exists and reports its version
      d. the network profile / UDP 5353 / Bonjour situation is sane
      e. an inbound firewall rule allows UDP 5353 (mDNS)
      f. the receiver actually advertises _airplay._tcp on the LAN
      g. python + zeroconf are available for (f)

    Real mirroring (video + audio from an iPhone) CANNOT be verified here.
    That stays MANUEL DOGRULAMA GEREKLI - see docs/SPEC.md.

    Every check prints [PASS] / [FAIL] / [WARN] / [SKIP] with a one-line reason and is
    repeated in a summary table. Exit code is 1 if any check FAILed, otherwise 0.

.PARAMETER MsysRoot
    MSYS2 installation root. Default: C:\msys64

.PARAMETER ExePath
    Path to the built uxplay.exe. Relative paths resolve against the repo root.
    Default: build\uxplay.exe

.PARAMETER Name
    AirPlay friendly name to advertise during the launch test. Default: AirPlay-PC

.PARAMETER SkipLaunch
    Skip check (f): do not start uxplay.exe.

.NOTES
    Check (f) needs a python interpreter that has the `zeroconf` package. Discovery order:
      1. $env:AIRPLAY_PYTHON
      2. <repo>\.venv\Scripts\python.exe
      3. python / py -3 from PATH
    Set AIRPLAY_PYTHON to point at a venv interpreter if the system python lacks zeroconf.

.EXAMPLE
    pwsh -NoProfile -File scripts\smoke-test.ps1 -SkipLaunch
#>

[CmdletBinding()]
param(
    [string]$MsysRoot = 'C:\msys64',
    [string]$ExePath  = 'build\uxplay.exe',
    [string]$Name     = 'AirPlay-PC',
    [switch]$SkipLaunch
)

$ErrorActionPreference = 'Continue'
$ProgressPreference    = 'SilentlyContinue'

# ---------------------------------------------------------------------------------
# Result bookkeeping
# ---------------------------------------------------------------------------------

$script:Results = New-Object System.Collections.ArrayList

function Add-Result {
    param(
        [Parameter(Mandatory)][string]$Check,
        [Parameter(Mandatory)][ValidateSet('PASS', 'FAIL', 'WARN', 'SKIP', 'INFO')][string]$Status,
        [string]$Reason = ''
    )
    $color = switch ($Status) {
        'PASS' { 'Green' }
        'FAIL' { 'Red' }
        'WARN' { 'Yellow' }
        'SKIP' { 'DarkGray' }
        default { 'Gray' }
    }
    Write-Host ('[{0}] ' -f $Status) -ForegroundColor $color -NoNewline
    Write-Host ('{0}' -f $Check) -NoNewline
    if ($Reason) { Write-Host (' - {0}' -f $Reason) -ForegroundColor DarkGray }
    else { Write-Host '' }

    [void]$script:Results.Add([pscustomobject]@{
        Status = $Status
        Check  = $Check
        Reason = $Reason
    })
}

function Write-Section {
    param([string]$Title)
    Write-Host ''
    Write-Host ('== {0} ' -f $Title).PadRight(78, '=') -ForegroundColor Cyan
}

function Write-Detail {
    param([string]$Text)
    Write-Host ('      {0}' -f $Text) -ForegroundColor DarkGray
}

# ---------------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------------

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not [System.IO.Path]::IsPathRooted($ExePath)) {
    $ExePath = Join-Path $repoRoot $ExePath
}

$ucrtBin     = Join-Path $MsysRoot 'ucrt64\bin'
$gstInspect  = Join-Path $ucrtBin  'gst-inspect-1.0.exe'
$appDataDir  = Join-Path $env:APPDATA   'airplay'
$localDir    = Join-Path $env:LOCALAPPDATA 'airplay'
$gstRegistry = Join-Path $localDir 'gst-registry.bin'
$mdnsScript  = Join-Path $PSScriptRoot 'mdns-browse.py'

Write-Host ''
Write-Host 'airplay smoke test (no iPhone required)' -ForegroundColor White
Write-Host ('repo      : {0}' -f $repoRoot)          -ForegroundColor DarkGray
Write-Host ('msys root : {0}' -f $MsysRoot)          -ForegroundColor DarkGray
Write-Host ('uxplay    : {0}' -f $ExePath)           -ForegroundColor DarkGray
Write-Host ('name      : {0}' -f $Name)              -ForegroundColor DarkGray

# ---------------------------------------------------------------------------------
# (a) MSYS2 UCRT64
# ---------------------------------------------------------------------------------

Write-Section 'a. MSYS2 UCRT64 toolchain'

$msysOk = Test-Path -LiteralPath $gstInspect
if ($msysOk) {
    $gstVersion = ''
    try { $gstVersion = (& $gstInspect --version 2>&1 | Select-Object -First 1) } catch { }
    Add-Result -Check 'MSYS2 UCRT64 present' -Status 'PASS' -Reason ("{0} ({1})" -f $gstInspect, $gstVersion)
} else {
    Add-Result -Check 'MSYS2 UCRT64 present' -Status 'FAIL' `
        -Reason ("{0} not found - run scripts\setup-msys2.ps1 (or pass -MsysRoot)" -f $gstInspect)
}

# ---------------------------------------------------------------------------------
# (b) GStreamer elements
# ---------------------------------------------------------------------------------

Write-Section 'b. GStreamer elements'

# Element -> MSYS2 package that owns it (docs/research/gstreamer-msys2-windows.md 3.2)
$requiredElements = [ordered]@{
    'appsrc'          = 'gst-plugins-base'
    'queue'           = 'gstreamer'
    'h264parse'       = 'gst-plugins-bad'
    'decodebin'       = 'gst-plugins-base'
    'avdec_h264'      = 'gst-libav'
    'videoconvert'    = 'gst-plugins-base'
    'videoscale'      = 'gst-plugins-base'
    'd3d11videosink'  = 'gst-plugins-bad'
    'autovideosink'   = 'gst-plugins-good'
    'avdec_aac'       = 'gst-libav'
    'avdec_alac'      = 'gst-libav'
    'audioconvert'    = 'gst-plugins-base'
    'audioresample'   = 'gst-plugins-base'
    'volume'          = 'gst-plugins-base'
    'level'           = 'gst-plugins-good'
    'wasapi2sink'     = 'gst-plugins-bad'
    'autoaudiosink'   = 'gst-plugins-good'
}

if (-not $msysOk) {
    Add-Result -Check ('GStreamer elements ({0})' -f $requiredElements.Count) -Status 'SKIP' `
        -Reason 'MSYS2 UCRT64 missing, cannot run gst-inspect-1.0'
} else {
    # Prepend the UCRT64 bin dir and use our own registry cache so we do not fight
    # with OBS / other GStreamer installs over %LOCALAPPDATA%\gstreamer-1.0\registry.*.bin
    $savedPath        = $env:PATH
    $savedGstRegistry = $env:GST_REGISTRY
    $missing          = New-Object System.Collections.ArrayList
    try {
        if (-not (Test-Path -LiteralPath $localDir)) {
            New-Item -ItemType Directory -Path $localDir -Force | Out-Null
        }
        $env:PATH         = "$ucrtBin;$savedPath"
        $env:GST_REGISTRY = $gstRegistry

        foreach ($element in $requiredElements.Keys) {
            $null = & $gstInspect $element 2>&1
            if ($LASTEXITCODE -eq 0) {
                Write-Detail ('OK   {0}' -f $element)
            } else {
                Write-Detail ('MISS {0}  (package: {1})' -f $element, $requiredElements[$element])
                [void]$missing.Add($element)
            }
        }
    } finally {
        $env:PATH = $savedPath
        if ($null -eq $savedGstRegistry) {
            Remove-Item Env:\GST_REGISTRY -ErrorAction SilentlyContinue
        } else {
            $env:GST_REGISTRY = $savedGstRegistry
        }
    }

    if ($missing.Count -eq 0) {
        Add-Result -Check ('GStreamer elements ({0})' -f $requiredElements.Count) -Status 'PASS' `
            -Reason ('all present; registry {0}' -f $gstRegistry)
    } else {
        $pkgs = ($missing | ForEach-Object { $requiredElements[$_] } | Sort-Object -Unique) -join ', '
        Add-Result -Check ('GStreamer elements ({0})' -f $requiredElements.Count) -Status 'FAIL' `
            -Reason ('missing: {0} -> install MSYS2 package(s): {1}' -f (($missing -join ', ')), $pkgs)
    }
}

# ---------------------------------------------------------------------------------
# (c) uxplay.exe
# ---------------------------------------------------------------------------------

Write-Section 'c. uxplay.exe'

$exeOk = Test-Path -LiteralPath $ExePath
if (-not $exeOk) {
    Add-Result -Check 'uxplay.exe exists' -Status 'FAIL' `
        -Reason ("{0} not found - run scripts\build.sh first" -f $ExePath)
    Add-Result -Check 'uxplay.exe -v' -Status 'SKIP' -Reason 'binary missing'
} else {
    Add-Result -Check 'uxplay.exe exists' -Status 'PASS' -Reason $ExePath

    # third_party/UxPlay/uxplay.cpp:1385-1387 handles "-v" and prints
    #   "UxPlay version <VERSION>; for help, use option \"-h\"" then exit(0).
    $savedPath = $env:PATH
    try {
        if ($msysOk) { $env:PATH = "$ucrtBin;$savedPath" }
        $verOut = & $ExePath -v 2>&1 | Select-Object -First 1
        $verRc  = $LASTEXITCODE
    } catch {
        $verOut = $_.Exception.Message
        $verRc  = -1
    } finally {
        $env:PATH = $savedPath
    }

    if ($verRc -eq 0 -and "$verOut" -match 'UxPlay version') {
        Add-Result -Check 'uxplay.exe -v' -Status 'PASS' -Reason ("$verOut".Trim())
    } elseif ($verRc -eq 0) {
        Add-Result -Check 'uxplay.exe -v' -Status 'WARN' `
            -Reason ("exit 0 but unexpected output: {0}" -f ("$verOut".Trim()))
    } else {
        Add-Result -Check 'uxplay.exe -v' -Status 'FAIL' `
            -Reason ("exit {0}: {1} (missing DLLs? MSYS2 ucrt64\bin must be on PATH)" -f $verRc, ("$verOut".Trim()))
    }
}

# ---------------------------------------------------------------------------------
# (d) Network
# ---------------------------------------------------------------------------------

Write-Section 'd. Network'

# --- network category ---
try {
    $profiles = @(Get-NetConnectionProfile -ErrorAction Stop)
} catch {
    $profiles = @()
    Add-Result -Check 'Network profile' -Status 'WARN' -Reason ('Get-NetConnectionProfile failed: {0}' -f $_.Exception.Message)
}

if ($profiles.Count -gt 0) {
    $public  = @($profiles | Where-Object { $_.NetworkCategory -eq 'Public' })
    $private = @($profiles | Where-Object { $_.NetworkCategory -eq 'Private' -or $_.NetworkCategory -eq 'DomainAuthenticated' })
    foreach ($p in $profiles) {
        Write-Detail ('{0} / {1} -> {2}' -f $p.InterfaceAlias, $p.Name, $p.NetworkCategory)
    }
    if ($private.Count -gt 0 -and $public.Count -eq 0) {
        Add-Result -Check 'Network profile is Private' -Status 'PASS' `
            -Reason (($private | ForEach-Object { '{0}={1}' -f $_.InterfaceAlias, $_.NetworkCategory }) -join ', ')
    } elseif ($private.Count -gt 0) {
        Add-Result -Check 'Network profile is Private' -Status 'WARN' `
            -Reason ('some interfaces are Public: {0} (mDNS/firewall rules are Private-scoped)' -f (($public | ForEach-Object { $_.InterfaceAlias }) -join ', '))
    } else {
        Add-Result -Check 'Network profile is Private' -Status 'WARN' `
            -Reason 'no Private interface; Set-NetConnectionProfile -NetworkCategory Private'
    }
}

# --- IPv4 addresses ---
try {
    $ips = @(Get-NetIPAddress -AddressFamily IPv4 -ErrorAction Stop |
             Where-Object { $_.IPAddress -notlike '127.*' -and $_.IPAddress -notlike '169.254.*' })
    foreach ($ip in $ips) { Write-Detail ('{0} : {1}' -f $ip.InterfaceAlias, $ip.IPAddress) }
    if ($ips.Count -gt 0) {
        Add-Result -Check 'IPv4 address' -Status 'PASS' `
            -Reason (($ips | ForEach-Object { '{0}={1}' -f $_.InterfaceAlias, $_.IPAddress }) -join ', ')
    } else {
        Add-Result -Check 'IPv4 address' -Status 'FAIL' -Reason 'no routable IPv4 address found'
    }
} catch {
    Add-Result -Check 'IPv4 address' -Status 'WARN' -Reason ('Get-NetIPAddress failed: {0}' -f $_.Exception.Message)
}

# --- UDP 5353 listeners (informational) ---
$listeners = @()
try {
    $endpoints = @(Get-NetUDPEndpoint -LocalPort 5353 -ErrorAction Stop)
    foreach ($ep in $endpoints) {
        $procName = '?'
        try { $procName = (Get-Process -Id $ep.OwningProcess -ErrorAction Stop).ProcessName } catch { }
        $listeners += ('{0} (pid {1}, {2})' -f $procName, $ep.OwningProcess, $ep.LocalAddress)
    }
} catch {
    # Fall back to netstat if the cmdlet is unavailable / access-denied.
    try {
        $netstat = @(netstat -ano -p UDP 2>$null | Select-String ':5353\s')
        foreach ($line in $netstat) {
            $fields = ($line.ToString() -split '\s+') | Where-Object { $_ }
            $procId = $fields[-1]
            $procName = '?'
            try { $procName = (Get-Process -Id ([int]$procId) -ErrorAction Stop).ProcessName } catch { }
            $listeners += ('{0} (pid {1})' -f $procName, $procId)
        }
    } catch { }
}

foreach ($l in $listeners) { Write-Detail ('UDP 5353 <- {0}' -f $l) }
if ($listeners.Count -gt 0) {
    # Windows svchost (DNS Client / dnscache) and Spotify also bind 5353. Multicast
    # binds are shared (SO_REUSEADDR), so this is informational, not a conflict.
    Add-Result -Check 'UDP 5353 listeners (informational)' -Status 'INFO' `
        -Reason (($listeners | Sort-Object -Unique) -join '; ')
} else {
    Add-Result -Check 'UDP 5353 listeners (informational)' -Status 'INFO' `
        -Reason 'nobody is bound to UDP 5353 right now'
}

# --- Apple Bonjour Service (competes for 5353, UxPlay issue #297) ---
$bonjour = $null
try { $bonjour = Get-Service -Name 'Bonjour Service' -ErrorAction Stop } catch { }
if ($null -eq $bonjour) {
    Add-Result -Check 'Apple Bonjour Service' -Status 'PASS' -Reason 'not installed (internal mDNS responder has 5353 to itself)'
} elseif ($bonjour.Status -eq 'Running') {
    Add-Result -Check 'Apple Bonjour Service' -Status 'WARN' `
        -Reason 'running - may win the race for UDP 5353 (UxPlay issue #297); consider -DUSE_DNS_SD=1 build or stopping it'
} else {
    Add-Result -Check 'Apple Bonjour Service' -Status 'PASS' -Reason ('installed but {0}' -f $bonjour.Status)
}

# ---------------------------------------------------------------------------------
# (e) Firewall
# ---------------------------------------------------------------------------------

Write-Section 'e. Firewall (inbound UDP 5353)'

function Get-FirewallRulesViaNetsh {
    # Get-NetFirewallRule returns "Access denied" on this machine unless elevated,
    # so netsh is the primary path. netsh field labels stay English even on a
    # localized Windows (verified on tr-TR Win10 22H2).
    $lines = @(netsh advfirewall firewall show rule name=all 2>$null)
    $rules = New-Object System.Collections.ArrayList
    $cur   = $null
    foreach ($line in $lines) {
        $text = "$line"
        if ($text -match '^Rule Name:\s*(.*)$') {
            if ($null -ne $cur) { [void]$rules.Add($cur) }
            $cur = @{ Name = $Matches[1].Trim() }
        } elseif ($null -ne $cur -and $text -match '^([A-Za-z][A-Za-z ]*):\s*(.*)$') {
            $cur[$Matches[1].Trim()] = $Matches[2].Trim()
        }
    }
    if ($null -ne $cur) { [void]$rules.Add($cur) }
    return $rules
}

$fwRules = @()
try {
    $fwRules = @(Get-FirewallRulesViaNetsh)
} catch {
    Add-Result -Check 'Firewall allows inbound UDP 5353' -Status 'WARN' `
        -Reason ('netsh advfirewall failed: {0}' -f $_.Exception.Message)
}

if ($fwRules.Count -gt 0) {
    $mdnsRules = @($fwRules | Where-Object {
        $_['Enabled']   -eq 'Yes' -and
        $_['Direction'] -eq 'In'  -and
        $_['Action']    -eq 'Allow' -and
        ($_['Protocol'] -eq 'UDP' -or $_['Protocol'] -eq 'Any') -and
        ($_['LocalPort'] -eq 'Any' -or ("," + ("$($_['LocalPort'])" -replace '\s', '') + ",") -like '*,5353,*')
    })

    $exactPort = @($mdnsRules | Where-Object { $_['LocalPort'] -ne 'Any' })
    $anyPort   = @($mdnsRules | Where-Object { $_['LocalPort'] -eq 'Any' })

    # Windows keeps many duplicate rules (one per browser install/update); dedupe for display.
    $exactNames = @($exactPort | ForEach-Object { $_['Name'] } | Sort-Object -Unique)
    $anyNames   = @($anyPort   | ForEach-Object { $_['Name'] } | Sort-Object -Unique)
    foreach ($n in ($exactNames | Select-Object -First 10)) { Write-Detail ('port 5353 : {0}' -f $n) }
    if ($exactNames.Count -gt 10) { Write-Detail ('... and {0} more distinct port-5353 rules' -f ($exactNames.Count - 10)) }
    foreach ($n in ($anyNames | Select-Object -First 5)) { Write-Detail ('any UDP   : {0}' -f $n) }
    if ($anyNames.Count -gt 5) { Write-Detail ('... and {0} more distinct program/any-port UDP rules' -f ($anyNames.Count - 5)) }

    if ($mdnsRules.Count -gt 0) {
        $names = (@($exactNames) + @($anyNames | Select-Object -First 3) | Select-Object -First 8) -join ', '
        Add-Result -Check 'Firewall allows inbound UDP 5353' -Status 'PASS' `
            -Reason ('{0} enabled inbound allow rule(s), e.g. {1}' -f $mdnsRules.Count, $names)
    } else {
        Add-Result -Check 'Firewall allows inbound UDP 5353' -Status 'FAIL' `
            -Reason 'no enabled inbound Allow rule covers UDP 5353 - mDNS discovery will not work'
    }

    $airplayRules = @($fwRules | Where-Object { $_['Name'] -like 'airplay:*' })
    if ($airplayRules.Count -gt 0) {
        foreach ($r in $airplayRules) {
            Write-Detail ('{0} [{1}/{2}/{3} {4}]' -f $r['Name'], $r['Direction'], $r['Protocol'], $r['LocalPort'], $r['Enabled'])
        }
        Add-Result -Check 'Project firewall rules (airplay:*)' -Status 'PASS' `
            -Reason ('{0} rule(s): {1}' -f $airplayRules.Count, (($airplayRules | ForEach-Object { $_['Name'] }) -join ', '))
    } else {
        Add-Result -Check 'Project firewall rules (airplay:*)' -Status 'WARN' `
            -Reason 'none found - run the firewall setup script (New-NetFirewallRule -DisplayName "airplay: ..." needs admin)'
    }
}

# ---------------------------------------------------------------------------------
# (g) Python + zeroconf (prerequisite for f)
# ---------------------------------------------------------------------------------

Write-Section 'g. Python + zeroconf'

function Resolve-PythonWithZeroconf {
    $candidates = New-Object System.Collections.ArrayList
    if ($env:AIRPLAY_PYTHON) { [void]$candidates.Add(@($env:AIRPLAY_PYTHON, @())) }
    $venvPy = Join-Path $repoRoot '.venv\Scripts\python.exe'
    if (Test-Path -LiteralPath $venvPy) { [void]$candidates.Add(@($venvPy, @())) }
    [void]$candidates.Add(@('python', @()))
    [void]$candidates.Add(@('py', @('-3')))

    foreach ($cand in $candidates) {
        $exe  = $cand[0]
        $pre  = @($cand[1])
        if (-not (Get-Command $exe -ErrorAction SilentlyContinue) -and -not (Test-Path -LiteralPath $exe)) { continue }
        $args = @($pre + @('-c', 'import zeroconf,sys; sys.stdout.write(zeroconf.__version__)'))
        $out  = & $exe @args 2>&1
        if ($LASTEXITCODE -eq 0) {
            return [pscustomobject]@{ Exe = $exe; Prefix = $pre; Version = ("$out".Trim()) }
        }
    }
    return $null
}

$python = Resolve-PythonWithZeroconf
if ($null -ne $python) {
    Add-Result -Check 'python + zeroconf' -Status 'PASS' `
        -Reason ('{0} {1} -> zeroconf {2}' -f $python.Exe, ($python.Prefix -join ' '), $python.Version)
} else {
    Add-Result -Check 'python + zeroconf' -Status 'WARN' `
        -Reason 'no interpreter with zeroconf found - python -m pip install zeroconf (or set AIRPLAY_PYTHON)'
}

if (-not (Test-Path -LiteralPath $mdnsScript)) {
    Add-Result -Check 'scripts\mdns-browse.py present' -Status 'FAIL' -Reason ("{0} missing" -f $mdnsScript)
} else {
    Add-Result -Check 'scripts\mdns-browse.py present' -Status 'PASS' -Reason $mdnsScript
}

# ---------------------------------------------------------------------------------
# (f) Launch + mDNS advertisement
# ---------------------------------------------------------------------------------

Write-Section 'f. Receiver advertises _airplay._tcp'

$launchSkipReason = $null
if ($SkipLaunch)                              { $launchSkipReason = '-SkipLaunch given' }
elseif (-not $msysOk)                         { $launchSkipReason = 'MSYS2 UCRT64 missing (check a failed)' }
elseif (-not $exeOk)                          { $launchSkipReason = 'uxplay.exe missing (check c failed)' }
elseif ($null -eq $python)                    { $launchSkipReason = 'no python with zeroconf - python -m pip install zeroconf' }
elseif (-not (Test-Path -LiteralPath $mdnsScript)) { $launchSkipReason = 'scripts\mdns-browse.py missing' }

if ($launchSkipReason) {
    Add-Result -Check 'receiver advertises _airplay._tcp on LAN' -Status 'SKIP' -Reason $launchSkipReason
} else {
    $proc      = $null
    $savedEnv  = @{
        PATH               = $env:PATH
        HOME               = $env:HOME
        XDG_CONFIG_HOMEDIR = $env:XDG_CONFIG_HOMEDIR
        GST_REGISTRY       = $env:GST_REGISTRY
    }
    $stdoutLog = Join-Path ([System.IO.Path]::GetTempPath()) ('uxplay-smoke-out-{0}.log' -f $PID)
    $stderrLog = Join-Path ([System.IO.Path]::GetTempPath()) ('uxplay-smoke-err-{0}.log' -f $PID)

    try {
        foreach ($dir in @($appDataDir, $localDir)) {
            if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
        }

        # uxplay.cpp:773-777 / :3153-3162 - without HOME / XDG_CONFIG_HOMEDIR the
        # persistent state (Ed25519 key pair, pairing records) is lost on Windows.
        $env:PATH               = "$ucrtBin;$($savedEnv.PATH)"
        $env:HOME               = $appDataDir
        $env:XDG_CONFIG_HOMEDIR = $appDataDir
        $env:GST_REGISTRY       = $gstRegistry

        $uxArgs = @('-n', $Name, '-p', '7100', '-vs', 'd3d11videosink', '-as', 'wasapi2sink')
        Write-Detail ('launching: {0} {1}' -f $ExePath, ($uxArgs -join ' '))

        $proc = Start-Process -FilePath $ExePath -ArgumentList $uxArgs -PassThru `
                              -WorkingDirectory $repoRoot `
                              -RedirectStandardOutput $stdoutLog -RedirectStandardError $stderrLog

        Start-Sleep -Seconds 4

        if ($proc.HasExited) {
            $tail = ''
            foreach ($log in @($stderrLog, $stdoutLog)) {
                if (Test-Path -LiteralPath $log) {
                    $tail = (Get-Content -LiteralPath $log -Tail 3 -ErrorAction SilentlyContinue) -join ' | '
                    if ($tail) { break }
                }
            }
            Add-Result -Check 'receiver advertises _airplay._tcp on LAN' -Status 'FAIL' `
                -Reason ('uxplay.exe exited early (code {0}): {1}' -f $proc.ExitCode, $tail)
        } else {
            $browseArgs = @($python.Prefix + @($mdnsScript, '--seconds', '6', '--expect', $Name))
            & $python.Exe @browseArgs
            $browseRc = $LASTEXITCODE

            if ($browseRc -eq 0) {
                Add-Result -Check 'receiver advertises _airplay._tcp on LAN' -Status 'PASS' `
                    -Reason ('mDNS service matching "{0}" seen within 6s' -f $Name)
            } elseif ($browseRc -eq 1) {
                Add-Result -Check 'receiver advertises _airplay._tcp on LAN' -Status 'FAIL' `
                    -Reason ('no _airplay._tcp service matching "{0}" - internal mDNS responder may be blocked on UDP 5353 (UxPlay issue #546)' -f $Name)
            } else {
                Add-Result -Check 'receiver advertises _airplay._tcp on LAN' -Status 'FAIL' `
                    -Reason ('mdns-browse.py exited {0}' -f $browseRc)
            }
        }
    } catch {
        Add-Result -Check 'receiver advertises _airplay._tcp on LAN' -Status 'FAIL' `
            -Reason ('launch failed: {0}' -f $_.Exception.Message)
    } finally {
        if ($null -ne $proc) {
            try {
                if (-not $proc.HasExited) {
                    Write-Detail ('stopping uxplay.exe (pid {0})' -f $proc.Id)
                    [void]$proc.CloseMainWindow()
                    if (-not $proc.WaitForExit(2000)) {
                        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
                    }
                }
            } catch {
                Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
            }
        }
        foreach ($key in @($savedEnv.Keys)) {
            $value = $savedEnv[$key]
            if ($null -eq $value) { Remove-Item ("Env:\{0}" -f $key) -ErrorAction SilentlyContinue }
            else { Set-Item ("Env:\{0}" -f $key) -Value $value }
        }
        foreach ($log in @($stdoutLog, $stderrLog)) {
            Remove-Item -LiteralPath $log -Force -ErrorAction SilentlyContinue
        }
    }
}

# ---------------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------------

Write-Section 'Summary'

$script:Results |
    Format-Table -AutoSize -Wrap -Property @(
        @{ Label = 'STATUS'; Expression = { $_.Status }; Width = 7 },
        @{ Label = 'CHECK';  Expression = { $_.Check } },
        @{ Label = 'DETAIL'; Expression = { $_.Reason } }
    ) | Out-String -Width 200 | Write-Host

$failCount = @($script:Results | Where-Object { $_.Status -eq 'FAIL' }).Count
$warnCount = @($script:Results | Where-Object { $_.Status -eq 'WARN' }).Count
$passCount = @($script:Results | Where-Object { $_.Status -eq 'PASS' }).Count
$skipCount = @($script:Results | Where-Object { $_.Status -eq 'SKIP' }).Count

Write-Host ('{0} PASS  {1} FAIL  {2} WARN  {3} SKIP' -f $passCount, $failCount, $warnCount, $skipCount) `
    -ForegroundColor $(if ($failCount -gt 0) { 'Red' } elseif ($warnCount -gt 0) { 'Yellow' } else { 'Green' })
Write-Host ''
Write-Host 'NOTE: real iPhone mirroring (video + audio) cannot be tested here -> MANUEL DOGRULAMA GEREKLI.' -ForegroundColor DarkGray
Write-Host ''

exit $(if ($failCount -gt 0) { 1 } else { 0 })
