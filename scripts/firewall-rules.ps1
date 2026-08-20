#Requires -RunAsAdministrator
<#
    firewall-rules.ps1 - inbound Windows Firewall rules for the AirPlay receiver.

    Every rule this script owns carries the DisplayName prefix "airplay:", so the
    script is idempotent by removing that whole set before recreating it. Nothing
    else on the machine is touched (the generic Windows "mDNS (UDP-Gelen)" rules
    stay as they are).

    Must run elevated: Get-NetFirewallRule already fails with "access denied" in a
    normal shell on this machine (docs/research/local-environment.md section 6).

    Usage (elevated PowerShell):
        pwsh -File scripts/firewall-rules.ps1
        pwsh -File scripts/firewall-rules.ps1 -ExePath C:\path\to\uxplay.exe
        pwsh -File scripts/firewall-rules.ps1 -Remove
#>

param(
    # Defaults to <repo>\build\uxplay.exe. -Program rules bind to the exact path, so
    # they must be recreated if the exe moves (research doc 7.2).
    [string] $ExePath = '',

    # Must match run-uxplay.ps1 -Port: "uxplay -p N" opens TCP+UDP N,N+1,N+2
    # (uxplay.cpp:1347-1352 + get_ports uxplay.cpp:1085-1090). 0 = legacy set only.
    [int] $Port = 7100,

    [switch] $Remove
)

$ErrorActionPreference = 'Stop'
$Prefix = 'airplay:'

# Belt-and-braces elevation check; #Requires already blocks the non-elevated case but
# its error message is cryptic.
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'This script must run in an elevated (Run as administrator) PowerShell.'
}

function Remove-AirplayRules {
    $existing = Get-NetFirewallRule -DisplayName "$Prefix*" -ErrorAction SilentlyContinue
    if ($existing) {
        foreach ($r in $existing) {
            Write-Host "removing: $($r.DisplayName)"
        }
        $existing | Remove-NetFirewallRule
    }
    else {
        Write-Host "no existing '$Prefix' rules found"
    }
}

# --- remove mode ------------------------------------------------------------
if ($Remove) {
    Remove-AirplayRules
    Write-Host 'done (rules removed)'
    exit 0
}

# --- resolve the exe --------------------------------------------------------
if (-not $ExePath) {
    $ExePath = Join-Path (Split-Path -Parent $PSScriptRoot) 'build\uxplay.exe'
}
if (-not (Test-Path -LiteralPath $ExePath)) {
    throw "uxplay.exe not found at '$ExePath'. Build it first (scripts/build.sh) or pass -ExePath."
}
$ExePath = (Resolve-Path -LiteralPath $ExePath).Path

# --- recreate ---------------------------------------------------------------
Remove-AirplayRules
Write-Host ''

# All rules are scoped to the Private profile: this machine's Ethernet connection is
# Private and Windows throttles discovery traffic on Public
# (docs/research/local-environment.md section 6, UxPlay README.md:1112-1122).
$common = @{ Action = 'Allow'; Profile = 'Private'; Enabled = 'True' }

$rules = @()

# 1. Program rule - covers every port the process listens on, including the dynamic
#    ones. This is what UxPlay's README recommends (README.md:1104-1110).
$rules += @{
    DisplayName = "$Prefix uxplay program (inbound)"
    Direction   = 'Inbound'
    Program     = $ExePath
    Protocol    = 'Any'
    Description = 'Allow all inbound traffic to the UxPlay AirPlay receiver executable.'
}

# 2/3. mDNS. Multicast 224.0.0.251 / ff02::fb, UDP 5353 (UxPlay lib/mdnsd/mdnsd.c:30).
#      Outbound is normally allowed by default policy, but declared explicitly so the
#      set is self-describing and survives a locked-down outbound policy.
$rules += @{
    DisplayName = "$Prefix mDNS UDP 5353 (inbound)"
    Direction   = 'Inbound'
    Protocol    = 'UDP'
    LocalPort   = '5353'
    Description = 'mDNS/DNS-SD service discovery for _airplay._tcp and _raop._tcp.'
}
$rules += @{
    DisplayName = "$Prefix mDNS UDP 5353 (outbound)"
    Direction   = 'Outbound'
    Protocol    = 'UDP'
    RemotePort  = '5353'
    Description = 'mDNS/DNS-SD announcements leaving this host.'
}

# 4/5. Legacy port set that a bare "uxplay -p" selects: TCP 7100,7000,7001 and
#      UDP 7011,6001,6000 (uxplay.cpp:1336-1337, README.md:1388-1397). The TCP side is
#      opened as the 7000-7100 range because AirPlay clients also probe neighbouring
#      ports on some iOS versions.
$rules += @{
    DisplayName = "$Prefix AirPlay TCP 7000-7100 (inbound)"
    Direction   = 'Inbound'
    Protocol    = 'TCP'
    LocalPort   = '7000-7100'
    Description = 'RAOP/AirPlay control and data TCP ports (legacy set 7000,7001,7100).'
}
$rules += @{
    DisplayName = "$Prefix AirPlay UDP legacy (inbound)"
    Direction   = 'Inbound'
    Protocol    = 'UDP'
    LocalPort   = '6000-6001,7011'
    Description = 'RAOP timing/control/audio UDP ports (legacy set 6000,6001,7011).'
}

# 6. The triple that "uxplay -p N" actually binds, when N is not inside the ranges above.
if ($Port -gt 0) {
    $portRange = "$Port-$($Port + 2)"
    $rules += @{
        DisplayName = "$Prefix session TCP $portRange (inbound)"
        Direction   = 'Inbound'
        Protocol    = 'TCP'
        LocalPort   = $portRange
        Description = "TCP ports opened by 'uxplay -p $Port'."
    }
    $rules += @{
        DisplayName = "$Prefix session UDP $portRange (inbound)"
        Direction   = 'Inbound'
        Protocol    = 'UDP'
        LocalPort   = $portRange
        Description = "UDP ports opened by 'uxplay -p $Port'."
    }
}

foreach ($rule in $rules) {
    $params = $common + $rule
    New-NetFirewallRule @params | Out-Null
    Write-Host "created : $($rule.DisplayName)"
}

Write-Host ''
Write-Host "program rule points at: $ExePath"
Write-Host "remove them again with: scripts/firewall-rules.ps1 -Remove"
