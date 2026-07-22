# common.ps1 - shared helpers for the Utgard client.
# Dot-sourced by every other script. Paths, state I/O, process
# control, config introspection, JSON output and list parsing.
# ENCODING: this file must be saved as UTF-8 with BOM.

# --- Colour scheme (16-colour console, Win10-safe) ------------------
# Requested palette #B2B1AE / #75837D / #974F3E / #344334 has no exact
# match in the 16-colour ConsoleColor enum that PS 5.1 supports, so
# these are the closest safe approximations. Named here so the whole
# UI pulls from one place.
$script:ColMain   = "Gray"       # #B2B1AE  основной текст
$script:ColMuted  = "DarkCyan"   # #75837D  второстепенный
$script:ColAccent = "DarkRed"    # #974F3E  акцент / предупреждение
$script:ColFrame  = "DarkGreen"  # #344334  рамки, заголовки
$script:ColOn     = "Green"      # состояние «включено»
$script:ColOff    = "DarkRed"    # состояние «выключено»

# --- Console encoding (required for Cyrillic output on PS 5.1) -----
try {
    [Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
    $OutputEncoding = [System.Text.UTF8Encoding]::new($false)
} catch {}

# --- Paths ----------------------------------------------------------
$script:BinDir       = Split-Path $PSCommandPath -Parent
$script:RootDir      = Split-Path $BinDir -Parent
$script:StateDir     = Join-Path $BinDir   "state"
$script:SbDir        = Join-Path $RootDir  "sing-box"
$script:ListDir      = Join-Path $RootDir  "lists"
$script:SbExe        = Join-Path $SbDir    "sing-box.exe"
$script:BaseConfig   = Join-Path $SbDir    "config.json"
$script:DefaultConfig= Join-Path $SbDir    "config.default.json"
$script:ConfigsDir   = Join-Path $SbDir    "configs"
$script:LastGoodConfig = Join-Path $SbDir  "config.last-good.json"
$script:ConfigBackup   = Join-Path $SbDir  "config.json.bak"
$script:GeneralTxt   = Join-Path $ListDir  "list-general.txt"
$script:GeneralSrs   = Join-Path $ListDir  "general.srs"
$script:GeneralJson  = Join-Path $ListDir  "general.json"
$script:LogDir       = Join-Path $RootDir "logs"
$script:LogFile      = Join-Path $LogDir  "sing-box.log"
$script:VersionFile  = Join-Path $RootDir  "VERSION"
$script:SbUrl        = "https://github.com/SagerNet/sing-box/releases/download/v1.13.14/sing-box-1.13.14-windows-amd64.zip"
# Expected SHA-256 of the archive above. This binary runs with
# administrator rights, so a tampered download (MITM despite TLS, or a
# compromised release asset) is a code-execution risk. Fill this in
# with the official hash for the pinned version to enforce
# verification. Leave it EMPTY to skip enforcement: the client then
# prints the hash it actually got so it can be checked by hand instead
# of failing to install. If you bump $SbUrl, update this too.
#   PowerShell: Get-FileHash .\sing-box-...-windows-amd64.zip -Algorithm SHA256
$script:SbSha256     = "f580782c6dd10f7691c66cea1d7c421813c5fbf7e305d1ee7ce0c3a40d196341"

$script:ActiveConfigsFile = Join-Path $StateDir "active-configs.txt"
$script:BuildStampFile    = Join-Path $StateDir "last-build.txt"

# Protocols we manage as "profiles". shadowsocks is listed already so
# that sync/reset handle it the moment import learns to parse ss://
$script:ProxyTypes = @("vless", "hysteria2", "shadowsocks")

# Log limits: whichever bites first
$script:LogKeepHours = 24
$script:LogMaxBytes  = 10MB

$script:Utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$script:Utf8Bom   = New-Object System.Text.UTF8Encoding($true)

# --- I/O helpers ----------------------------------------------------
function Write-Utf8($path, $text) {
    $dir = Split-Path $path -Parent
    if ($dir -and -not (Test-Path $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }
    [System.IO.File]::WriteAllText($path, $text, $script:Utf8NoBom)
}

function Write-Utf8Bom($path, $text) {
    # For files the USER opens in Notepad: without a BOM, PS 5.1
    # falls back to ANSI and mangles Cyrillic comments.
    $dir = Split-Path $path -Parent
    if ($dir -and -not (Test-Path $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }
    [System.IO.File]::WriteAllText($path, $text, $script:Utf8Bom)
}

function Read-Lines($path) {
    if (-not (Test-Path $path)) {
        return @()
    }
    return @(Get-Content $path -Encoding UTF8 |
        ForEach-Object { $_.Trim() } |
        Where-Object { $_ -ne "" })
}

function Ensure-Dirs {
    foreach ($d in @($StateDir, $ListDir, $SbDir, $LogDir, $ConfigsDir)) {
        if (-not (Test-Path $d)) {
            New-Item -ItemType Directory -Path $d -Force | Out-Null
        }
    }
    if (-not (Test-Path $GeneralTxt)) {
        $seed = @()
        $seed += "# Адреса, которые пойдут через VPN."
        $seed += "# По одному в строке. Строки с # - комментарии."
        $seed += "# Поддомены учитываются автоматически:"
        $seed += "# spotify.com покрывает и audio.spotify.com"
        $seed += "# Можно добавлять IP и подсети: 203.0.113.0/24"
        $seed += ""
        Write-Utf8Bom $GeneralTxt (($seed -join "`r`n") + "`r`n")
    }
}

function Ensure-SingBox {
    # Download sing-box on first run if it is missing.
    # Returns $true if the exe is available (was already there or
    # downloaded successfully), $false if it could not be obtained.
    if (Test-Path $SbExe) {
        return $true
    }
    Write-Host ""
    Write-Host "  sing-box не найден — скачиваю..." -ForegroundColor Cyan
    Write-Host ("  {0}" -f $SbUrl) -ForegroundColor DarkGray
    Write-Host ""

    $tmpZip = Join-Path $env:TEMP "sing-box-download.zip"
    $tmpDir = Join-Path $env:TEMP "sing-box-extract"
    try {
        [Net.ServicePointManager]::SecurityProtocol = `
            [Net.SecurityProtocolType]::Tls12

        $oldProgress = $ProgressPreference
        $ProgressPreference = 'SilentlyContinue'
        try {
            Invoke-WebRequest -Uri $SbUrl -OutFile $tmpZip `
                -UseBasicParsing -ErrorAction Stop
        } finally {
            $ProgressPreference = $oldProgress
        }

        if (-not (Test-Path $tmpZip)) {
            throw "файл не загружен"
        }

        # --- integrity check ----------------------------------------
        # The exe we are about to unpack runs as administrator. Verify
        # the archive against the pinned hash before touching it.
        $actualHash = (Get-FileHash -Path $tmpZip -Algorithm SHA256).Hash
        if ($script:SbSha256) {
            $expected = $script:SbSha256.Trim().ToUpper()
            if ($actualHash.ToUpper() -ne $expected) {
                throw ("контрольная сумма не совпала. Ожидалось {0}, получено {1}. " -f `
                    $expected, $actualHash) + "Файл повреждён или подменён — установка прервана."
            }
            Write-Host "  Контрольная сумма совпала." -ForegroundColor Green
        } else {
            Write-Host "  ВНИМАНИЕ: проверка контрольной суммы отключена." `
                -ForegroundColor Yellow
            Write-Host ("  SHA-256 скачанного файла:") -ForegroundColor DarkGray
            Write-Host ("  {0}" -f $actualHash) -ForegroundColor DarkGray
            Write-Host "  Сверьте его с официальным на странице релиза sing-box." `
                -ForegroundColor DarkGray
        }

        if (Test-Path $tmpDir) {
            Remove-Item $tmpDir -Recurse -Force -ErrorAction SilentlyContinue
        }
        Expand-Archive -Path $tmpZip -DestinationPath $tmpDir -Force

        $sub = Get-ChildItem -Path $tmpDir -Directory |
            Select-Object -First 1
        $srcDir = if ($sub) { $sub.FullName } else { $tmpDir }

        $exe = Get-ChildItem -Path $srcDir -Filter "sing-box.exe" `
            -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if (-not $exe) {
            throw "в архиве не найден sing-box.exe"
        }

        Get-ChildItem -Path $srcDir -File | ForEach-Object {
            Copy-Item $_.FullName (Join-Path $SbDir $_.Name) -Force
        }
        Write-Host "  sing-box установлен." -ForegroundColor Green
        return $true
    } catch {
        Write-Host "  Не удалось скачать sing-box." -ForegroundColor Red
        Write-Host ("  {0}" -f $_.Exception.Message) -ForegroundColor Yellow
        Write-Host ""
        Write-Host "  Скачайте вручную и положите в папку sing-box\:" `
            -ForegroundColor Yellow
        Write-Host ("  {0}" -f $SbUrl) -ForegroundColor DarkGray
        return $false
    } finally {
        Remove-Item $tmpZip -Force -ErrorAction SilentlyContinue
        Remove-Item $tmpDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Get-ClientVersion {
    if (Test-Path $VersionFile) {
        $v = (Get-Content $VersionFile -Raw).Trim()
        if ($v) {
            return $v
        }
    }
    return "?"
}

function Get-BootId {
    # Windows boot time as a stable per-session id. Two menu launches
    # in the same uptime session get the same string; anything across
    # a reboot (clean or not) gets a different one.
    try {
        $os = Get-CimInstance Win32_OperatingSystem -ErrorAction Stop
        return $os.LastBootUpTime.ToString("yyyyMMddHHmmss")
    } catch {
        # CIM unavailable: fall back to uptime-derived boot moment
        try {
            $ticks = [Environment]::TickCount64
            $boot  = (Get-Date).AddMilliseconds(-$ticks)
            return $boot.ToString("yyyyMMddHHmm")
        } catch {
            return ""
        }
    }
}

# --- JSON output ----------------------------------------------------
function Format-Json([string]$json) {
    # PS 5.1 ConvertTo-Json indents unpredictably and makes the file
    # unpleasant to read or hand-edit. Re-indent the compressed form
    # ourselves: two spaces per level, one value per line.
    $sb = New-Object System.Text.StringBuilder
    $unit = "  "
    $indent = 0
    $inStr = $false
    $esc = $false

    for ($i = 0; $i -lt $json.Length; $i++) {
        $c = $json[$i]

        if ($inStr) {
            [void]$sb.Append($c)
            if ($esc) {
                $esc = $false
            } elseif ($c -eq '\') {
                $esc = $true
            } elseif ($c -eq '"') {
                $inStr = $false
            }
            continue
        }

        if ($c -eq '"') {
            $inStr = $true
            [void]$sb.Append($c)
        }
        elseif ($c -eq '{' -or $c -eq '[') {
            $close = '}'
            if ($c -eq '[') {
                $close = ']'
            }
            if (($i + 1) -lt $json.Length -and $json[$i + 1] -eq $close) {
                [void]$sb.Append([string]$c + [string]$close)
                $i++
            } else {
                $indent++
                [void]$sb.Append([string]$c + "`r`n" + ($unit * $indent))
            }
        }
        elseif ($c -eq '}' -or $c -eq ']') {
            $indent--
            [void]$sb.Append("`r`n" + ($unit * $indent) + [string]$c)
        }
        elseif ($c -eq ',') {
            [void]$sb.Append(",`r`n" + ($unit * $indent))
        }
        elseif ($c -eq ':') {
            [void]$sb.Append(": ")
        }
        else {
            [void]$sb.Append($c)
        }
    }
    return $sb.ToString()
}

function Write-JsonConfig($path, $obj) {
    $raw = $obj | ConvertTo-Json -Depth 30 -Compress
    Write-Utf8 $path ((Format-Json $raw) + "`r`n")
}

# --- State: active extra configs ------------------------------------
function Get-ActiveConfigs {
    # Overlay file names, stored in active-configs.txt, validated to
    # exist in the configs\ folder. System configs live one level up
    # and can never appear here.
    $names = Read-Lines $ActiveConfigsFile
    $valid = @()
    foreach ($n in $names) {
        if ($n -like 'example-*') {
            continue
        }
        $p = Join-Path $ConfigsDir $n
        if (Test-Path $p) {
            $valid += $n
        }
    }
    return $valid
}

function Get-ConfigTitle([string]$fileName) {
    # Display name from the file name only. No sidecar, no "_title"
    # field inside the JSON: sing-box 1.13 rejects unknown fields on
    # decode, so nothing extra may live in the config.
    # discord.json -> "Discord"; star-citizen.json -> "Star Citizen"
    $stem = $fileName -replace '\.json$',''
    if (-not $stem) {
        return $fileName
    }
    $words = $stem -split '[-_]' | Where-Object { $_ }
    $out = @()
    foreach ($w in $words) {
        $out += $w.Substring(0,1).ToUpper() + $w.Substring(1)
    }
    return ($out -join ' ')
}

function Get-ReadyOverlays {
    # Overlays that are enabled AND currently valid together with the
    # base config - i.e. really ready to route traffic. Returns their
    # display titles. This is what the header should show, not just
    # "enabled", so a broken-but-remembered overlay does not claim to
    # be active.
    $active = @(Get-ActiveConfigs)
    if ($active.Count -eq 0) {
        return @()
    }
    $titles = @()
    foreach ($name in $active) {
        $res = Test-ConfigSet @($name)
        if ($res.Ok) {
            $titles += (Get-ConfigTitle $name)
        }
    }
    return $titles
}

function Set-ActiveConfigs($names) {
    $clean = @($names |
        Where-Object {
            $_ -and $_ -ne "config.json" -and $_ -ne "config.default.json"
        } |
        Sort-Object -Unique)
    Write-Utf8 $ActiveConfigsFile (($clean -join "`r`n") + "`r`n")
}

# --- Default (shipped) config ---------------------------------------
$script:TemplateCache = $null

function Get-TemplateOutbounds {
    # The proxy outbounds exactly as shipped in config.default.json.
    # Single source of truth for "what an unconfigured profile is".
    if ($null -ne $script:TemplateCache) {
        return $script:TemplateCache
    }
    $out = @()
    if (Test-Path $DefaultConfig) {
        try {
            $d = Get-Content $DefaultConfig -Raw -Encoding UTF8 |
                ConvertFrom-Json
            foreach ($o in $d.outbounds) {
                if ($script:ProxyTypes -contains $o.type) {
                    $out += $o
                }
            }
        } catch {
            $out = @()
        }
    }
    $script:TemplateCache = $out
    return $out
}

function Test-PlaceholderOutbound($o) {
    # An outbound that still matches the shipped template is "not
    # configured yet". We require BOTH the template server AND the
    # template credentials to still be in place: a real profile that
    # happens to sit on the template host (e.g. example.com in a test
    # setup) must not be mistaken for an empty stub and silently
    # dropped. The shipped stubs match on both counts, so dropping
    # leftover templates on import keeps working.
    $tpl = @(Get-TemplateOutbounds | Where-Object { $_.tag -eq $o.tag })
    if ($tpl.Count -gt 0) {
        $t = $tpl[0]
        if ($o.server -ne $t.server) {
            return $false
        }
        switch ($o.type) {
            "vless" {
                if ($t.PSObject.Properties.Name -contains 'uuid') {
                    return ($o.uuid -eq $t.uuid)
                }
            }
            "hysteria2" {
                if ($t.PSObject.Properties.Name -contains 'password') {
                    return ($o.password -eq $t.password)
                }
            }
            "shadowsocks" {
                if ($t.PSObject.Properties.Name -contains 'password') {
                    return ($o.password -eq $t.password)
                }
            }
        }
        # server matched but we could not compare credentials: fall
        # back to the conservative "server match means stub" rule
        return $true
    }
    if ($o.server -eq "example.com") {
        return $true
    }
    if ($o.password -eq "changeme") {
        return $true
    }
    if ($o.uuid -eq "00000000-0000-0000-0000-000000000000") {
        return $true
    }
    if ($o.tls -and $o.tls.reality) {
        if ($o.tls.reality.public_key -eq "changeme") {
            return $true
        }
    }
    return $false
}

# --- Config introspection -------------------------------------------
function Get-ProxyOutbounds {
    if (-not (Test-Path $BaseConfig)) {
        return @()
    }
    try {
        $cfg = Get-Content $BaseConfig -Raw -Encoding UTF8 |
            ConvertFrom-Json
    } catch {
        return @()
    }
    $out = @()
    foreach ($o in $cfg.outbounds) {
        if ($script:ProxyTypes -contains $o.type) {
            $out += [PSCustomObject]@{
                Tag         = $o.tag
                Type        = $o.type
                Server      = $o.server
                Port        = $o.server_port
                Placeholder = (Test-PlaceholderOutbound $o)
            }
        }
    }
    return $out
}

function Test-ProfileConfigured {
    $real = @(Get-ProxyOutbounds | Where-Object { -not $_.Placeholder })
    return ($real.Count -gt 0)
}

# --- Config safety net ------------------------------------------------
# Two different files, two different jobs:
#   config.json.bak       - short-lived, only while one operation runs,
#                           removed on every exit path (it holds real
#                           credentials and must not linger)
#   config.last-good.json - snapshot of a config that passed
#                           validation, used to recover a damaged
#                           config.json instead of losing the profile

function New-ConfigBackup {
    if (Test-Path $BaseConfig) {
        Copy-Item $BaseConfig $ConfigBackup -Force
    }
}

function Remove-ConfigBackup {
    if (Test-Path $ConfigBackup) {
        Remove-Item $ConfigBackup -Force -ErrorAction SilentlyContinue
    }
}

function Restore-ConfigBackup {
    if (Test-Path $ConfigBackup) {
        Copy-Item $ConfigBackup $BaseConfig -Force
    }
    Remove-ConfigBackup
}

function Save-LastGood {
    if (Test-Path $BaseConfig) {
        Copy-Item $BaseConfig $LastGoodConfig -Force
    }
}

function Read-BaseConfig {
    # Parse config.json. If it is damaged, fall back to the last
    # validated snapshot and put it back in place, so a corrupted
    # file costs the user nothing.
    # Returns @{ Cfg = <object or $null>; Recovered = $true/$false }
    $res = @{ Cfg = $null; Recovered = $false }

    if (Test-Path $BaseConfig) {
        try {
            $res.Cfg = Get-Content $BaseConfig -Raw -Encoding UTF8 |
                ConvertFrom-Json
            return $res
        } catch {
        }
    }

    if (Test-Path $LastGoodConfig) {
        try {
            $cfg = Get-Content $LastGoodConfig -Raw -Encoding UTF8 |
                ConvertFrom-Json
            Copy-Item $LastGoodConfig $BaseConfig -Force
            $res.Cfg = $cfg
            $res.Recovered = $true
            return $res
        } catch {
        }
    }

    if (Test-Path $DefaultConfig) {
        try {
            $cfg = Get-Content $DefaultConfig -Raw -Encoding UTF8 |
                ConvertFrom-Json
            Copy-Item $DefaultConfig $BaseConfig -Force
            $res.Cfg = $cfg
            $res.Recovered = $true
            return $res
        } catch {
        }
    }

    return $res
}

function Insert-AfterActions($rules, $newRule) {
    # Place $newRule right after the leading action-only rules
    # (sniff / hijack-dns): before them the domain or ip is not yet
    # known when the rule is evaluated.
    $rules = @($rules)
    $pos = 0
    for ($i = 0; $i -lt $rules.Count; $i++) {
        $isAction = $rules[$i].PSObject.Properties.Name -contains 'action'
        $isRoute  = $rules[$i].PSObject.Properties.Name -contains 'outbound'
        if ($isAction -and -not $isRoute) {
            $pos = $i + 1
        }
    }
    $merged = @()
    if ($pos -gt 0) {
        $merged += $rules[0..($pos - 1)]
    }
    $merged += $newRule
    if ($pos -lt $rules.Count) {
        $merged += $rules[$pos..($rules.Count - 1)]
    }
    return @($merged)
}

# --- Config rewriting ------------------------------------------------
function Set-BypassRules($cfg, $srvHosts, $srvIps) {
    # The proxy server's own address must bypass the tunnel, otherwise
    # we loop through ourselves. Two cases, handled separately:
    #   - hostnames: resolved by the SYSTEM resolver (dns rule) and
    #     sent DIRECT by a domain route rule
    #   - bare IPs: no name to resolve, sent DIRECT by an ip_cidr rule
    # Everything is rebuilt from scratch so old servers never pile up.
    $srvHosts = @($srvHosts)
    $srvIps   = @($srvIps)
    $noHosts  = ($srvHosts.Count -eq 0)
    $noIps    = ($srvIps.Count -eq 0)

    # --- dns: resolve server names locally --------------------------
    # (IPs need no DNS rule; only hostnames appear here)
    if ($noHosts) {
        if ($cfg.dns -and $cfg.dns.rules) {
            $cfg.dns.rules = @($cfg.dns.rules | Where-Object {
                $hasDomain = $_.PSObject.Properties.Name -contains 'domain'
                -not ($hasDomain -and $_.server -eq 'local')
            })
        }
    } else {
        $found = $false
        if ($cfg.dns -and $cfg.dns.rules) {
            foreach ($r in $cfg.dns.rules) {
                $hasDomain = $r.PSObject.Properties.Name -contains 'domain'
                if ($hasDomain -and $r.server -eq 'local') {
                    $r.domain = @($srvHosts)
                    $found = $true
                }
            }
        }
        if (-not $found -and $cfg.dns) {
            $newRule = [PSCustomObject]@{
                domain = @($srvHosts)
                server = "local"
            }
            # must precede the rule_set rule that sends names to DoH
            $cfg.dns.rules = @($newRule) + @($cfg.dns.rules)
        }
    }

    if (-not $cfg.route) {
        return
    }

    # --- route, domain bypass (hostnames) ---------------------------
    if ($noHosts) {
        if ($cfg.route.rules) {
            $cfg.route.rules = @($cfg.route.rules | Where-Object {
                $hasDomain = $_.PSObject.Properties.Name -contains 'domain'
                -not ($hasDomain -and $_.outbound -eq 'direct')
            })
        }
    } else {
        $found = $false
        if ($cfg.route.rules) {
            foreach ($r in $cfg.route.rules) {
                $hasDomain = $r.PSObject.Properties.Name -contains 'domain'
                if ($hasDomain -and $r.outbound -eq 'direct') {
                    $r.domain = @($srvHosts)
                    $found = $true
                }
            }
        }
        if (-not $found) {
            $newRule = [PSCustomObject]@{
                domain   = @($srvHosts)
                action   = "route"
                outbound = "direct"
            }
            $cfg.route.rules = Insert-AfterActions $cfg.route.rules $newRule
        }
    }

    # --- route, ip bypass (bare-IP servers) -------------------------
    # A server given as an IP is matched here explicitly, instead of
    # relying on route.final = direct catching it by luck.
    if ($noIps) {
        if ($cfg.route.rules) {
            $cfg.route.rules = @($cfg.route.rules | Where-Object {
                $hasIp = $_.PSObject.Properties.Name -contains 'ip_cidr'
                $isSrv = $_.PSObject.Properties.Name -contains '_server_ip'
                -not ($hasIp -and $isSrv -and $_.outbound -eq 'direct')
            })
        }
    } else {
        $found = $false
        if ($cfg.route.rules) {
            foreach ($r in $cfg.route.rules) {
                $isSrv = $r.PSObject.Properties.Name -contains '_server_ip'
                if ($isSrv -and $r.outbound -eq 'direct') {
                    $r.ip_cidr = @($srvIps)
                    $found = $true
                }
            }
        }
        if (-not $found) {
            $newRule = [PSCustomObject]@{
                ip_cidr    = @($srvIps)
                action     = "route"
                outbound   = "direct"
                _server_ip = $true
            }
            $cfg.route.rules = Insert-AfterActions $cfg.route.rules $newRule
        }
    }
}

function Sync-ProxyConfig($cfg) {
    # Bring the derived parts of the config in line with whatever
    # proxy outbounds are actually present: the profile group and the
    # server-bypass rules. Shared by import, reset and the profile
    # picker so they can never drift apart. Returns the proxy tags.
    $proxyTags = @()
    foreach ($o in $cfg.outbounds) {
        if ($script:ProxyTypes -contains $o.type) {
            $proxyTags += $o.tag
        }
    }

    foreach ($o in $cfg.outbounds) {
        if ($o.type -ne "urltest" -and $o.type -ne "selector") {
            continue
        }
        # No latency-based auto-picking any more: the user states
        # which profile to use. Configs written by older versions
        # still carry "urltest" and are converted in place.
        if ($o.type -eq "urltest") {
            $o.type = "selector"
            foreach ($f in @('url', 'interval', 'tolerance', 'idle_timeout')) {
                if ($o.PSObject.Properties.Name -contains $f) {
                    $o.PSObject.Properties.Remove($f)
                }
            }
        }

        $o.outbounds = @($proxyTags)

        $cur = ""
        if ($o.PSObject.Properties.Name -contains 'default') {
            $cur = [string]$o.default
        }
        $fallback = ""
        if ($proxyTags.Count -gt 0) {
            $fallback = $proxyTags[0]
        }
        if ($fallback -and ($proxyTags -notcontains $cur)) {
            # the chosen profile is gone (or none was set): fall back
            # to the first real profile rather than let the group pick
            if ($o.PSObject.Properties.Name -contains 'default') {
                $o.default = $fallback
            } else {
                $o | Add-Member -NotePropertyName default `
                    -NotePropertyValue $fallback
            }
        }
    }

    $srvHosts = @()
    $srvIps = @()
    foreach ($o in $cfg.outbounds) {
        if ($script:ProxyTypes -contains $o.type -and $o.server) {
            if (Test-IsIp $o.server) {
                $ip = $o.server
                if ($ip -notmatch '/') {
                    $ip = "$ip/32"
                }
                $srvIps += $ip
            } else {
                $srvHosts += $o.server
            }
        }
    }
    $srvHosts = @($srvHosts | Sort-Object -Unique)
    $srvIps = @($srvIps | Sort-Object -Unique)
    Set-BypassRules $cfg $srvHosts $srvIps
    return $proxyTags
}

function Get-ActiveProfile {
    # Tag of the profile the selector will dial. "" if unknown.
    if (-not (Test-Path $BaseConfig)) {
        return ""
    }
    try {
        $cfg = Get-Content $BaseConfig -Raw -Encoding UTF8 | ConvertFrom-Json
    } catch {
        return ""
    }
    foreach ($o in $cfg.outbounds) {
        if ($o.tag -eq "utgard") {
            if ($o.PSObject.Properties.Name -contains 'default') {
                return [string]$o.default
            }
            return ""
        }
    }
    return ""
}

function Set-ActiveProfile([string]$tag) {
    # Point the selector at $tag and persist. Returns $true on success.
    if (-not (Test-Path $BaseConfig)) {
        return $false
    }
    try {
        $cfg = Get-Content $BaseConfig -Raw -Encoding UTF8 | ConvertFrom-Json
    } catch {
        return $false
    }

    $tags = @(Sync-ProxyConfig $cfg)
    if ($tags -notcontains $tag) {
        return $false
    }
    foreach ($o in $cfg.outbounds) {
        if ($o.tag -eq "utgard") {
            if ($o.PSObject.Properties.Name -contains 'default') {
                $o.default = $tag
            } else {
                $o | Add-Member -NotePropertyName default -NotePropertyValue $tag
            }
        }
    }
    Write-JsonConfig $BaseConfig $cfg
    return $true
}

# --- List info ------------------------------------------------------
function Get-ListCount {
    return @(Read-DomainFile $GeneralTxt).Count
}

function Get-ListStamp {
    # Header format: "hh:mm dd.MM.yyyy". Returns "" if never built.
    if (-not (Test-Path $BuildStampFile)) {
        return ""
    }
    $raw = (Get-Content $BuildStampFile -Raw).Trim()
    try {
        $d = [datetime]::ParseExact($raw, "yyyy-MM-dd HH:mm:ss", $null)
        return $d.ToString("HH:mm dd.MM.yyyy")
    } catch {
        return $raw
    }
}

# --- Error translation ----------------------------------------------
function ConvertTo-FriendlyError($chk) {
    $t = ($chk | Out-String)
    if ($t -match 'public_key|invalid uuid|changeme|invalid password') {
        return "Профиль настроен неверно или устарел. Импортируйте ссылку заново."
    }
    if ($t -match 'permission denied|Access is denied') {
        return "Недостаточно прав. Запустите start-proxy.bat от администратора."
    }
    if ($t -match 'address already in use|bind:') {
        return "Порт занят. Возможно, уже работает другой VPN-клиент."
    }
    if ($t -match 'no such file|cannot find|open .*\.srs') {
        return "Не найден файл списка. Пересоберите список в разделе [6]."
    }
    return $null
}

# --- Log maintenance --------------------------------------------------
function Limit-LogSize {
    # Hard cap on file size. Runs before the age-based pass so that a
    # runaway log is bounded first and the line-by-line filter never
    # has to stream gigabytes. Reads only the tail via a seek, so
    # memory use does not depend on how big the file got.
    $fi = Get-Item $LogFile -ErrorAction SilentlyContinue
    if (-not $fi) {
        return
    }
    if ($fi.Length -le $script:LogMaxBytes) {
        return
    }

    $tmp = "$LogFile.tmp"
    try {
        $fs = [System.IO.File]::Open($LogFile, [System.IO.FileMode]::Open, `
            [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
        try {
            $fs.Seek(-$script:LogMaxBytes, [System.IO.SeekOrigin]::End) |
                Out-Null
            $out = [System.IO.File]::Create($tmp)
            try {
                $buf = New-Object byte[] 65536
                $first = $true
                while ($true) {
                    $n = $fs.Read($buf, 0, $buf.Length)
                    if ($n -le 0) {
                        break
                    }
                    $start = 0
                    if ($first) {
                        # the cut lands mid-line: drop that fragment
                        for ($i = 0; $i -lt $n; $i++) {
                            if ($buf[$i] -eq 10) {
                                $start = $i + 1
                                break
                            }
                        }
                        $first = $false
                    }
                    if ($n -gt $start) {
                        $out.Write($buf, $start, $n - $start)
                    }
                }
            } finally {
                $out.Close()
            }
        } finally {
            $fs.Close()
        }
        Move-Item $tmp $LogFile -Force
    } catch {
        if (Test-Path $tmp) {
            Remove-Item $tmp -Force -ErrorAction SilentlyContinue
        }
    }
}

function Trim-Log {
    # sing-box has no log rotation of its own. Two independent limits:
    # size (so the disk cannot fill) and age (so the window stays
    # predictable for troubleshooting). Only safe while the file is
    # not held open, so this runs just before start.
    if (-not (Test-Path $LogFile)) {
        return
    }
    if (Get-SbProc) {
        return
    }

    Limit-LogSize

    $cutoff = (Get-Date).AddHours(-$script:LogKeepHours)
    $tmp = "$LogFile.tmp"
    $keeping = $false
    $rx = [regex]'\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}'

    try {
        $reader = New-Object System.IO.StreamReader($LogFile)
        $writer = New-Object System.IO.StreamWriter($tmp, $false, $script:Utf8NoBom)
        while ($null -ne ($line = $reader.ReadLine())) {
            if (-not $keeping) {
                $m = $rx.Match($line)
                if ($m.Success) {
                    try {
                        $ts = [datetime]::ParseExact($m.Value, `
                            "yyyy-MM-dd HH:mm:ss", $null)
                        if ($ts -ge $cutoff) {
                            $keeping = $true
                        }
                    } catch {}
                }
            }
            if ($keeping) {
                $writer.WriteLine($line)
            }
        }
        $reader.Close()
        $writer.Close()
        Move-Item $tmp $LogFile -Force
    } catch {
        if (Test-Path $tmp) {
            Remove-Item $tmp -Force -ErrorAction SilentlyContinue
        }
    }
}

# --- Process control -------------------------------------------------
function Get-SbProc {
    # Only OUR sing-box. Reading .Path of an exiting process throws,
    # so a failed read simply means "not ours".
    Get-Process -Name "sing-box" -ErrorAction SilentlyContinue |
        Where-Object {
            try { $_.Path -eq $SbExe } catch { $false }
        }
}

function Get-SbConfigArgs {
    $a = @("-c", $BaseConfig)
    foreach ($c in (Get-ActiveConfigs)) {
        $a += @("-c", (Join-Path $ConfigsDir $c))
    }
    return $a
}

function Get-SbArgs {
    return @("run") + (Get-SbConfigArgs)
}

function Test-ConfigSet($overlayNames) {
    # Validate base config plus the given overlays without touching
    # saved state. Used before enabling an overlay.
    $result = @{ Ok = $false; Output = @("sing-box.exe not found") }
    if (-not (Test-Path $SbExe)) {
        return $result
    }
    $a = @("-c", $BaseConfig)
    foreach ($n in @($overlayNames)) {
        $a += @("-c", (Join-Path $ConfigsDir $n))
    }
    $out = & $SbExe check @a 2>&1
    $result.Ok = ($LASTEXITCODE -eq 0)
    $result.Output = $out
    return $result
}

function Start-Sb {
    if (Get-SbProc) {
        return $true
    }
    if (-not (Test-Path $SbExe)) {
        Write-Host "  Не найден sing-box.exe:" -ForegroundColor Red
        Write-Host "  $SbExe" -ForegroundColor DarkYellow
        return $false
    }
    $chk = & $SbExe check @(Get-SbConfigArgs) 2>&1
    if ($LASTEXITCODE -ne 0) {
        $friendly = ConvertTo-FriendlyError $chk
        Write-Host ""
        if ($friendly) {
            Write-Host "  $friendly" -ForegroundColor Red
        } else {
            Write-Host "  Настройки содержат ошибку, запуск отменён." `
                -ForegroundColor Red
        }
        Write-Host ""
        Write-Host "  Технические подробности:" -ForegroundColor DarkGray
        $chk | ForEach-Object {
            Write-Host "    $_" -ForegroundColor DarkGray
        }
        return $false
    }
    Trim-Log
    if (-not (Test-Path $LogDir)) {
        New-Item -ItemType Directory -Path $LogDir -Force | Out-Null
    }
    Start-Process -FilePath $SbExe -ArgumentList (Get-SbArgs) `
        -WorkingDirectory $RootDir -WindowStyle Hidden

    # Wait for the process to appear, then confirm it is still alive a
    # moment later. A fixed single sleep would report success for a
    # sing-box that starts and immediately dies (e.g. a TUN adapter
    # conflict), leaving the crash to surface only on the next menu
    # visit. Poll instead, and re-check after a short settle delay.
    $proc = $null
    for ($i = 0; $i -lt 6; $i++) {
        Start-Sleep -Milliseconds 300
        $proc = Get-SbProc
        if ($proc) {
            break
        }
    }
    if (-not $proc) {
        return $false
    }
    # settle: catch an early crash (bad TUN, port grabbed after check)
    Start-Sleep -Milliseconds 700
    return [bool](Get-SbProc)
}

function Stop-Sb {
    # Ask politely first: a hard kill leaves sing-box no chance to
    # tear down the TUN adapter and its routes, which is how people
    # end up with "the internet died after I turned the VPN off".
    $p = Get-SbProc
    if (-not $p) {
        return $true
    }
    $procId = $p.Id

    & taskkill.exe /PID $procId 2>&1 | Out-Null

    $deadline = (Get-Date).AddSeconds(6)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 300
        if (-not (Get-SbProc)) {
            Trim-Log
            return $true
        }
    }

    $p = Get-SbProc
    if ($p) {
        Write-Host "  Не отвечает, завершаю принудительно..." `
            -ForegroundColor Yellow
        $p | Stop-Process -Force -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 700
    }
    Trim-Log
    return (-not (Get-SbProc))
}

function Test-ServerReachable($server, $port) {
    if (-not $server -or -not $port) {
        return $false
    }
    try {
        $c = New-Object System.Net.Sockets.TcpClient
        $iar = $c.BeginConnect($server, $port, $null, $null)
        $ok = $iar.AsyncWaitHandle.WaitOne(3000)
        if (-not $ok) {
            $c.Close()
            return $false
        }
        $c.EndConnect($iar)
        $c.Close()
        return $true
    } catch {
        return $false
    }
}

# --- Domain-list parsing ---------------------------------------------
function Test-IsIp([string]$s) {
    # Strict IPv4 / IPv4-CIDR check. Each octet must be 0-255 and the
    # optional mask 0-32. A loose \d{1,3} form would let "999.1.1.1"
    # or "10.0.0.0/99" through: build-lists would then bolt a "/32"
    # onto garbage and the rule-set compiler would choke with no clue
    # for the user. Anything that fails here is treated as a domain.
    $oct = '(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])'
    $mask = '([0-9]|[12][0-9]|3[0-2])'
    return ($s -match ("^{0}(\.{0}){{3}}(/{1})?$" -f $oct, $mask))
}

function Normalize-Domain([string]$raw) {
    $l = ($raw -split '#')[0].Trim()
    if (-not $l) {
        return $null
    }
    # IP or CIDR: return untouched. The cleanup below splits on '/'
    # and would silently turn 203.0.113.0/24 into a single host.
    if (Test-IsIp $l) {
        return $l
    }
    $l = $l -replace '^https?://','' -replace '^\|\|','' -replace '\^$','' `
            -replace '^\+\.','' -replace '^\*\.','' -replace '^\.',''
    $l = ($l -split '/')[0]
    $l = ($l -split '\s+')[0]
    return $l.ToLower()
}

function Read-DomainFile($path) {
    if (-not (Test-Path $path)) {
        return @()
    }
    $out = @()
    foreach ($line in (Get-Content $path -Encoding UTF8)) {
        $d = Normalize-Domain $line
        if ($d) {
            $out += $d
        }
    }
    return $out
}