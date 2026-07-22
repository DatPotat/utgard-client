# import-link.ps1 - parse a vless:// , hysteria2:// or ss:// share
# link and write the resulting outbound into the BASE config.json.
# Drops leftover placeholder outbounds, then hands the derived parts
# (urltest group, server-bypass rules) to Sync-ProxyConfig.
# Backs up the config first and rolls back if validation fails.
# ENCODING: this file must be saved as UTF-8 with BOM.

param([string]$Link)

. (Join-Path (Split-Path $PSCommandPath -Parent) "common.ps1")

# Ciphers sing-box actually implements. Historic stream ciphers
# (aes-256-cfb, rc4-md5, ...) are gone and must be rejected here,
# otherwise the user gets an opaque FATAL from sing-box.
$script:SsMethods = @(
    "none",
    "aes-128-gcm", "aes-192-gcm", "aes-256-gcm",
    "chacha20-ietf-poly1305", "xchacha20-ietf-poly1305",
    "2022-blake3-aes-128-gcm", "2022-blake3-aes-256-gcm",
    "2022-blake3-chacha20-poly1305"
)

function Parse-Query([string]$q) {
    $h = @{}
    if (-not $q) {
        return $h
    }
    foreach ($pair in ($q.TrimStart('?') -split '&')) {
        if (-not $pair) {
            continue
        }
        $kv  = $pair -split '=', 2
        $key = [uri]::UnescapeDataString($kv[0])
        $val = ""
        if ($kv.Count -gt 1) {
            $val = [uri]::UnescapeDataString($kv[1])
        }
        $h[$key] = $val
    }
    return $h
}

function ConvertFrom-Base64Url([string]$s) {
    # ss:// links use base64url without padding; some clients still
    # emit standard base64. Accept both, return "" on failure so the
    # caller can fall back to the plain-text form.
    if (-not $s) {
        return ""
    }
    $t = $s.Replace('-', '+').Replace('_', '/')
    $rem = $t.Length % 4
    if ($rem -eq 2) {
        $t += "=="
    } elseif ($rem -eq 3) {
        $t += "="
    } elseif ($rem -eq 1) {
        return ""
    }
    try {
        $bytes = [Convert]::FromBase64String($t)
        return [System.Text.Encoding]::UTF8.GetString($bytes)
    } catch {
        return ""
    }
}

function Split-HostPort([string]$hostPart) {
    # Handles both host:port and [v6::addr]:port
    $srvHost = ""
    $srvPort = 0
    if ($hostPart.StartsWith('[')) {
        $end = $hostPart.IndexOf(']')
        if ($end -lt 0) {
            throw "неверный адрес сервера в ссылке"
        }
        $srvHost = $hostPart.Substring(1, $end - 1)
        $tail = $hostPart.Substring($end + 1)
        if ($tail.StartsWith(':')) {
            $tail = $tail.Substring(1)
            if ($tail -match '^\d+$') {
                $srvPort = [int]$tail
            }
        }
    } else {
        $lc = $hostPart.LastIndexOf(':')
        if ($lc -lt 0) {
            throw "в ссылке не указан порт"
        }
        $srvHost = $hostPart.Substring(0, $lc)
        $portStr = $hostPart.Substring($lc + 1)
        if (-not ($portStr -match '^\d+$')) {
            throw "неверный порт в ссылке"
        }
        $srvPort = [int]$portStr
    }
    if (-not $srvHost) {
        throw "в ссылке нет адреса сервера"
    }
    if ($srvPort -le 0 -or $srvPort -gt 65535) {
        throw "неверный порт в ссылке"
    }
    return @{ Host = $srvHost; Port = $srvPort }
}

function ConvertFrom-SsLink([string]$link) {
    # Two incompatible shapes exist in the wild:
    #   SIP002  ss://base64url(method:password)@host:port/?plugin=..#tag
    #   legacy  ss://base64(method:password@host:port)#tag
    # The legacy one is not a valid URI, so no [System.Uri] here.
    $rest = $link.Substring(5)

    $hashPos = $rest.IndexOf('#')
    if ($hashPos -ge 0) {
        $rest = $rest.Substring(0, $hashPos)
    }
    if (-not $rest) {
        throw "ссылка ss:// пустая"
    }

    $query = ""
    $qPos = $rest.IndexOf('?')
    if ($qPos -ge 0) {
        $query = $rest.Substring($qPos)
        $rest = $rest.Substring(0, $qPos)
    }
    $rest = $rest.TrimEnd('/')

    $userInfo = ""
    $hostPart = ""

    $atPos = $rest.LastIndexOf('@')
    if ($atPos -lt 0) {
        $decoded = ConvertFrom-Base64Url $rest
        if (-not $decoded) {
            throw "не удалось разобрать ss:// — повреждённая ссылка"
        }
        $atPos = $decoded.LastIndexOf('@')
        if ($atPos -lt 0) {
            throw "в ссылке ss:// нет адреса сервера"
        }
        $userInfo = $decoded.Substring(0, $atPos)
        $hostPart = $decoded.Substring($atPos + 1)
    } else {
        $rawUser  = $rest.Substring(0, $atPos)
        $hostPart = $rest.Substring($atPos + 1)
        $userInfo = ConvertFrom-Base64Url $rawUser
        if (-not ($userInfo -match ':')) {
            # some clients put method:password in plain percent-encoding
            $userInfo = [uri]::UnescapeDataString($rawUser)
        }
    }

    $colon = $userInfo.IndexOf(':')
    if ($colon -lt 0) {
        throw "в ссылке ss:// не разобрать метод и пароль"
    }
    $method   = $userInfo.Substring(0, $colon).ToLower()
    $password = $userInfo.Substring($colon + 1)

    if ($script:SsMethods -notcontains $method) {
        throw ("метод шифрования '{0}' не поддерживается — " -f $method) +
              "нужен современный ss (AEAD или 2022-blake3)"
    }
    if (-not $password -and $method -ne "none") {
        throw "в ссылке ss:// нет пароля"
    }

    $hp = Split-HostPort $hostPart

    $out = [ordered]@{
        type        = "shadowsocks"
        tag         = "utgard-ss"
        server      = $hp.Host
        server_port = $hp.Port
        method      = $method
        password    = $password
    }

    $q = Parse-Query $query
    if ($q['plugin']) {
        $p = $q['plugin']
        $semi = $p.IndexOf(';')
        if ($semi -ge 0) {
            $pName = $p.Substring(0, $semi)
            $pOpts = $p.Substring($semi + 1)
        } else {
            $pName = $p
            $pOpts = ""
        }
        if ($pName -eq 'simple-obfs') {
            $pName = 'obfs-local'
        }
        if ($pName -ne 'obfs-local' -and $pName -ne 'v2ray-plugin') {
            throw ("плагин '{0}' не поддерживается " -f $pName) +
                  "(доступны obfs-local и v2ray-plugin)"
        }
        $out.plugin = $pName
        $out.plugin_opts = $pOpts
    }

    return @{ outbound = $out; host = $hp.Host }
}

function ConvertFrom-ProxyLink([string]$link) {
    $link = $link.Trim()

    if ($link -match '^ss://') {
        return ConvertFrom-SsLink $link
    }
    if ($link -match '^(ssr|vmess|trojan|tuic|anytls|socks|http)://') {
        throw ("протокол '{0}' пока не поддерживается — " -f $Matches[1]) +
              "нужна vless://, hysteria2:// или ss://"
    }

    $u = [System.Uri]$link
    $q = Parse-Query $u.Query
    $userInfo = [uri]::UnescapeDataString($u.UserInfo)
    $port = 443
    if ($u.Port -gt 0) {
        $port = $u.Port
    }
    if (-not $u.Host) {
        throw "в ссылке нет адреса сервера"
    }

    $sni = $u.Host
    if ($q['sni']) {
        $sni = $q['sni']
    }

    switch -Regex ($u.Scheme) {
        '^(hysteria2|hy2)$' {
            $out = [ordered]@{
                type        = "hysteria2"
                tag         = "utgard-hy2"
                server      = $u.Host
                server_port = $port
                password    = $userInfo
            }
            if ($q['obfs']) {
                $out.obfs = [ordered]@{
                    type     = $q['obfs']
                    password = $q['obfs-password']
                }
            }
            $out.tls = [ordered]@{
                enabled     = $true
                server_name = $sni
                alpn        = @("h3")
            }
            if ($q['insecure'] -eq '1') {
                $out.tls.insecure = $true
            }
            return @{ outbound = $out; host = $u.Host }
        }
        '^vless$' {
            if (-not $userInfo) {
                throw "в ссылке vless нет идентификатора (uuid)"
            }
            # Decide whether this vless link carries TLS at all.
            #   security=reality -> TLS + REALITY
            #   security=tls     -> plain TLS
            #   security=none    -> no transport security
            #   security absent  -> infer: pbk => reality, else a
            #                       server_name/vision hint => tls,
            #                       otherwise treat as plain (no tls).
            # Forcing tls.enabled=true on a non-TLS server (the old
            # behaviour) produced a silent handshake failure the user
            # could not diagnose.
            $security = ""
            if ($q['security']) {
                $security = $q['security'].ToLower()
            }
            $isReality = $false
            $useTls = $false
            if ($security -eq 'reality') {
                $isReality = $true
                $useTls = $true
            } elseif ($security -eq 'tls' -or $security -eq 'xtls') {
                $useTls = $true
            } elseif ($security -eq 'none') {
                $useTls = $false
            } else {
                # security not stated: infer conservatively
                if ($q['pbk']) {
                    $isReality = $true
                    $useTls = $true
                } elseif ($q['sni'] -or $q['flow']) {
                    $useTls = $true
                }
            }

            $out = [ordered]@{
                type        = "vless"
                tag         = "utgard-vless"
                server      = $u.Host
                server_port = $port
                uuid        = $userInfo
            }

            if ($useTls) {
                if ($isReality -and -not $q['pbk']) {
                    throw "в ссылке reality нет ключа (pbk)"
                }
                $fp = "chrome"
                if ($q['fp']) {
                    $fp = $q['fp']
                }
                $tls = [ordered]@{
                    enabled     = $true
                    server_name = $sni
                    utls        = [ordered]@{
                        enabled     = $true
                        fingerprint = $fp
                    }
                }
                if ($isReality) {
                    $sid = ""
                    if ($q['sid']) {
                        $sid = $q['sid']
                    }
                    $tls.reality = [ordered]@{
                        enabled    = $true
                        public_key = $q['pbk']
                        short_id   = $sid
                    }
                }
                # flow (xtls-rprx-vision) only makes sense with TLS;
                # attaching it to a plain vless is invalid.
                if ($q['flow']) {
                    $out.flow = $q['flow']
                }
                $out.tls = $tls
            }
            return @{ outbound = $out; host = $u.Host }
        }
        default {
            throw ("неизвестный тип ссылки '{0}' — " -f $u.Scheme) +
                  "нужна vless://, hysteria2:// или ss://"
        }
    }
}

# --- main ----------------------------------------------------------
Clear-Host
Write-Host "===== ИМПОРТ ПРОФИЛЯ =====" -ForegroundColor Cyan
Write-Host ""
Write-Host "  Вставьте ссылку, которую вам дал владелец сервера." `
    -ForegroundColor Gray
Write-Host "  Подходят ссылки vless:// , hysteria2:// и ss://" `
    -ForegroundColor Gray
Write-Host "  Вставка: правая кнопка мыши в этом окне." `
    -ForegroundColor DarkGray
Write-Host "  Чтобы отменить — просто нажмите Enter." `
    -ForegroundColor DarkGray
Write-Host ""

if (-not $Link) {
    $Link = Read-Host "Ссылка"
}
if (-not $Link) {
    Write-Host "Отменено." -ForegroundColor Yellow
    return
}

try {
    $parsed = ConvertFrom-ProxyLink $Link
} catch {
    Write-Host ""
    Write-Host "  Ссылка не распознана: $_" -ForegroundColor Red
    Write-Host "  Проверьте, что скопировали её целиком." `
        -ForegroundColor Yellow
    return
}

$read = Read-BaseConfig
if ($null -eq $read.Cfg) {
    Write-Host ""
    Write-Host "  Файл настроек повреждён, и восстановить его" `
        -ForegroundColor Red
    Write-Host "  не из чего." -ForegroundColor Red
    Write-Host "  Скопируйте config.default.json в config.json" `
        -ForegroundColor Yellow
    Write-Host "  и повторите импорт." -ForegroundColor Yellow
    return
}
if ($read.Recovered) {
    Write-Host ""
    Write-Host "  Файл настроек был повреждён — восстановлен" `
        -ForegroundColor Yellow
    Write-Host "  из последней рабочей копии." -ForegroundColor Yellow
}
$cfg = $read.Cfg

New-ConfigBackup

$newOut = [PSCustomObject]$parsed.outbound
$newTag = $newOut.tag

# replace our outbound, drop other protocols' unconfigured templates
$kept = @()
$replaced = $false
$dropped = @()
foreach ($o in $cfg.outbounds) {
    if ($o.tag -eq $newTag) {
        $kept += $newOut
        $replaced = $true
        continue
    }
    if ($ProxyTypes -contains $o.type) {
        if (Test-PlaceholderOutbound $o) {
            $dropped += $o.tag
            continue
        }
    }
    $kept += $o
}
if (-not $replaced) {
    $kept += $newOut
}
$cfg.outbounds = $kept

# urltest group + server bypass rules, in one place
$proxyTags = @(Sync-ProxyConfig $cfg)

Write-JsonConfig $BaseConfig $cfg

# validate; roll back on failure
if (Test-Path $SbExe) {
    $chk = & $SbExe check -c $BaseConfig 2>&1
    if ($LASTEXITCODE -ne 0) {
        Restore-ConfigBackup
        Write-Host ""
        Write-Host "  Профиль не подошёл, настройки возвращены" `
            -ForegroundColor Red
        Write-Host "  к прежнему состоянию." -ForegroundColor Red
        $friendly = ConvertTo-FriendlyError $chk
        if ($friendly) {
            Write-Host ("  {0}" -f $friendly) -ForegroundColor Yellow
        }
        Write-Host "  Технические подробности:" -ForegroundColor DarkGray
        $chk | ForEach-Object {
            Write-Host "    $_" -ForegroundColor DarkGray
        }
        return
    }
    # config is proven good: keep a snapshot for recovery
    Save-LastGood
} else {
    Write-Host ""
    Write-Host "  Не найден sing-box.exe — профиль сохранён" `
        -ForegroundColor Yellow
    Write-Host "  без проверки." -ForegroundColor Yellow
}

Remove-ConfigBackup

Write-Host ""
Write-Host "  Профиль импортирован." -ForegroundColor Green
Write-Host ("  протокол : {0}" -f $newOut.type)
Write-Host ("  сервер   : {0}:{1}" -f $newOut.server, $newOut.server_port)
if ($newOut.tls -and $newOut.tls.server_name) {
    Write-Host ("  маскировка под : {0}" -f $newOut.tls.server_name) `
        -ForegroundColor DarkGray
}
if ($newOut.method) {
    Write-Host ("  шифрование : {0}" -f $newOut.method) `
        -ForegroundColor DarkGray
}
if ($newOut.plugin) {
    Write-Host ("  плагин : {0}" -f $newOut.plugin) -ForegroundColor DarkGray
}
if ($dropped.Count) {
    Write-Host ("  убраны пустые заготовки : {0}" -f ($dropped -join ", ")) `
        -ForegroundColor DarkGray
}

Write-Host ""
$act = Get-ActiveProfile
if ($proxyTags.Count -le 1) {
    Write-Host "  Профиль будет использоваться для подключения." `
        -ForegroundColor Gray
} elseif ($act -eq $newTag) {
    Write-Host "  Этот профиль назначен активным." -ForegroundColor Gray
} else {
    Write-Host ("  Профилей теперь {0}, но активным остаётся" -f `
        $proxyTags.Count) -ForegroundColor Yellow
    Write-Host "  прежний. Чтобы переключиться, вернитесь" `
        -ForegroundColor Yellow
    Write-Host "  в меню [4] и отметьте нужный цифрой." `
        -ForegroundColor Yellow
}
Write-Host ""
Write-Host "  Чтобы профиль заработал, включите VPN: [1]." `
    -ForegroundColor Yellow