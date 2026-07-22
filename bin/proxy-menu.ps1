# proxy-menu.ps1 - Utgard client control panel (Russian UI).
# Launched by start-proxy.bat. Requires Administrator (TUN device).
# sing-box is NEVER started automatically: only [1] starts it.
# ENCODING: this file must be saved as UTF-8 with BOM.

. (Join-Path (Split-Path $PSCommandPath -Parent) "common.ps1")

# ---- Administrator check ------------------------------------------
$identity  = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
$adminRole = [Security.Principal.WindowsBuiltInRole]::Administrator
if (-not $principal.IsInRole($adminRole)) {
    Write-Host ""
    Write-Host "===============================================" `
        -ForegroundColor $ColAccent
    Write-Host ""
    Write-Host "  Нужны права администратора." -ForegroundColor $ColAccent
    Write-Host ""
    Write-Host "  Закройте это окно, затем щёлкните по файлу"
    Write-Host "  start-proxy.bat ПРАВОЙ кнопкой мыши и"
    Write-Host "  выберите `"Запуск от имени администратора`"."
    Write-Host ""
    Write-Host "===============================================" `
        -ForegroundColor $ColAccent
    Write-Host ""
    Read-Host "Enter — закрыть окно" | Out-Null
    exit 1
}

Ensure-Dirs
Set-Location $RootDir

$script:WantedFile  = Join-Path $StateDir "vpn-wanted.txt"
$script:BootFile    = Join-Path $StateDir "vpn-wanted-boot.txt"
$script:ChosenFile  = Join-Path $StateDir "profile-chosen.txt"
$script:CrashNotice = $false

function Pause-Menu {
    Read-Host "`nEnter — продолжить" | Out-Null
}

# ---- watchdog state --------------------------------------------------
function Set-VpnWanted([bool]$on) {
    $v = "0"
    if ($on) {
        $v = "1"
    }
    Write-Utf8 $WantedFile $v
    if ($on) {
        Write-Utf8 $BootFile (Get-BootId)
    }
}

function Get-VpnWanted {
    if (-not (Test-Path $WantedFile)) {
        return $false
    }
    return ((Get-Content $WantedFile -Raw).Trim() -eq "1")
}

function Test-WantedThisBoot {
    if (-not (Test-Path $BootFile)) {
        return $false
    }
    $stamped = (Get-Content $BootFile -Raw).Trim()
    if (-not $stamped) {
        return $false
    }
    return ($stamped -eq (Get-BootId))
}

function Show-CrashAlert([string]$when) {
    Clear-Host
    Write-Host "======================================" -ForegroundColor $ColAccent
    Write-Host "         ВНИМАНИЕ: VPN УПАЛ"           -ForegroundColor $ColAccent
    Write-Host "======================================" -ForegroundColor $ColAccent
    Write-Host ""
    Write-Host ("  {0}" -f $when) -ForegroundColor $ColAccent
    Write-Host ""
    Write-Host "  Пока VPN выключен, весь трафик идёт"
    Write-Host "  напрямую — сайты из вашего списка"
    Write-Host "  открываются без защиты или не"
    Write-Host "  открываются вовсе."
    Write-Host ""
    Write-Host "  Нажмите [1], чтобы включить снова."
    Write-Host "======================================" -ForegroundColor $ColAccent
    Pause-Menu
}

# ---- explicit profile choice -----------------------------------------
function Get-ChosenProfile {
    if (-not (Test-Path $ChosenFile)) {
        return ""
    }
    return (Get-Content $ChosenFile -Raw).Trim()
}

function Set-ChosenProfile([string]$tag) {
    Write-Utf8 $ChosenFile $tag
}

function Get-RealProfiles {
    return @(Get-ProxyOutbounds | Where-Object { -not $_.Placeholder })
}

function Test-ProfileReady {
    $profiles = @(Get-RealProfiles)
    if ($profiles.Count -eq 0) {
        return @{ Ok = $false; Reason = "none" }
    }
    if ($profiles.Count -eq 1) {
        if ((Get-ActiveProfile) -ne $profiles[0].Tag) {
            Set-ActiveProfile $profiles[0].Tag | Out-Null
        }
        Set-ChosenProfile $profiles[0].Tag
        return @{ Ok = $true }
    }
    $chosen = Get-ChosenProfile
    $tags = @($profiles | ForEach-Object { $_.Tag })
    if (-not $chosen -or ($tags -notcontains $chosen)) {
        return @{ Ok = $false; Reason = "pick" }
    }
    if ((Get-ActiveProfile) -ne $chosen) {
        Set-ActiveProfile $chosen | Out-Null
    }
    return @{ Ok = $true }
}

# ---- legacy config conversion ----------------------------------------
function Convert-LegacyGroup {
    if (-not (Test-Path $BaseConfig)) {
        return $false
    }
    try {
        $cfg = Get-Content $BaseConfig -Raw -Encoding UTF8 | ConvertFrom-Json
    } catch {
        return $false
    }
    $legacy = $false
    foreach ($o in $cfg.outbounds) {
        if ($o.type -eq "urltest") {
            $legacy = $true
        }
    }
    if (-not $legacy) {
        return $false
    }
    Sync-ProxyConfig $cfg | Out-Null
    Write-JsonConfig $BaseConfig $cfg
    return $true
}

# ---- hints -----------------------------------------------------------
function Show-RestartHint {
    Write-Host ""
    Write-Host "  Изменения сохранены." -ForegroundColor $ColAccent
    Write-Host "  Чтобы они заработали, перезапустите VPN:" `
        -ForegroundColor $ColAccent
    Write-Host "  сначала [2], затем [1]." -ForegroundColor $ColAccent
}

function Show-ListApplied {
    Write-Host ""
    Write-Host "  Список сохранён и применяется сразу," `
        -ForegroundColor $ColOn
    Write-Host "  перезапускать VPN не нужно." -ForegroundColor $ColOn
}

# ---- header: profile table -------------------------------------------
function Show-ProfileBlock {
    $profiles = @(Get-RealProfiles)
    if ($profiles.Count -eq 0) {
        Write-Host "  Профили: " -NoNewline -ForegroundColor $ColMain
        Write-Host "Нет загруженного профиля. Добавить — пункт [4]" `
            -ForegroundColor $ColMuted
        return
    }
    $act = Get-ActiveProfile
    Write-Host "  Профили:" -ForegroundColor $ColMain
    foreach ($p in $profiles) {
        $proto = switch ($p.Type) {
            "hysteria2"   { "Hysteria2" }
            "vless"       { "VLESS" }
            "shadowsocks" { "ShadowSocks" }
            default       { $p.Type }
        }
        $line = "    {0,-11}  {1,-20}" -f $proto, $p.Server
        if ($p.Tag -eq $act) {
            Write-Host $line -NoNewline -ForegroundColor $ColMain
            Write-Host "— Активен" -ForegroundColor $ColOn
        } else {
            Write-Host $line -ForegroundColor $ColMain
        }
    }
}

# ---- header: overlay line --------------------------------------------
function Get-ConfigLine {
    $ready = @(Get-ReadyOverlays)
    if ($ready.Count -eq 0) {
        return "Нет"
    }
    return ($ready -join ", ")
}

# ---- profile submenu -------------------------------------------------
function Menu-Profile {
    while ($true) {
        $profiles = @(Get-RealProfiles)
        $act = Get-ActiveProfile

        Clear-Host
        Write-Host "===== ПРОФИЛИ VPN =====" -ForegroundColor $ColFrame
        Write-Host ""
        if ($profiles.Count -eq 0) {
            Write-Host "  Профилей нет." -ForegroundColor $ColAccent
            Write-Host "  Вставьте ссылку от владельца сервера — [N]." `
                -ForegroundColor $ColMuted
        } else {
            for ($i = 0; $i -lt $profiles.Count; $i++) {
                $mark = "[ ]"
                if ($profiles[$i].Tag -eq $act) {
                    $mark = "[x]"
                }
                Write-Host ("  {0} {1}. {2} — {3}" -f `
                    $mark, ($i + 1), $profiles[$i].Type, $profiles[$i].Server) `
                    -ForegroundColor $ColMain
            }
            if ($profiles.Count -gt 1) {
                Write-Host ""
                Write-Host "  Через VPN работает только отмеченный профиль." `
                    -ForegroundColor $ColMuted
            }
        }
        Write-Host "-----------------------"
        if ($profiles.Count -gt 1) {
            Write-Host "  цифра — сделать активным"
        }
        Write-Host "  [N] импортировать новый профиль"
        Write-Host "  [D] удалить профили"
        Write-Host "  [B] назад"
        $k = (Read-Host "Выбор").ToUpper()

        if ($k -eq "B") {
            break
        }
        elseif ($k -eq "N") {
            & (Join-Path $BinDir "import-link.ps1")
            Pause-Menu
        }
        elseif ($k -eq "D") {
            & (Join-Path $BinDir "reset-profiles.ps1")
            Pause-Menu
        }
        elseif ($k -match '^\d+$') {
            $n = [int]$k
            if ($n -ge 1 -and $n -le $profiles.Count) {
                $tag = $profiles[$n - 1].Tag
                if (Set-ActiveProfile $tag) {
                    Set-ChosenProfile $tag
                    if (Get-SbProc) {
                        Show-RestartHint
                        Pause-Menu
                    }
                } else {
                    Write-Host "  Не удалось переключить профиль." `
                        -ForegroundColor $ColAccent
                    Pause-Menu
                }
            }
        }
    }
}

# ---- config manager (overlays) ---------------------------------------
function Menu-Configs {
    $changed = $false
    while ($true) {
        $extra = @(Get-ChildItem -Path $ConfigsDir -Filter "*.json" `
            -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -notlike 'example-*' } |
            Select-Object -ExpandProperty Name | Sort-Object)
        $active = @(Get-ActiveConfigs)

        Clear-Host
        Write-Host "===== ИЗМЕНЕНИЕ КОНФИГУРАЦИИ =====" -ForegroundColor $ColFrame
        Write-Host "  Дополнительные конфиги: приложения, игры," `
            -ForegroundColor $ColMuted
        Write-Host "  рабочая сеть. Основной конфиг работает всегда." `
            -ForegroundColor $ColMuted
        Write-Host ""
        if ($extra.Count -eq 0) {
            Write-Host "  Дополнительных конфигов нет." -ForegroundColor $ColAccent
            Write-Host "  Как их создать — см. README.md" -ForegroundColor $ColMuted
        } else {
            for ($i = 0; $i -lt $extra.Count; $i++) {
                $mark = "[ ]"
                if ($active -contains $extra[$i]) {
                    $mark = "[x]"
                }
                $title = Get-ConfigTitle $extra[$i]
                Write-Host ("  {0} {1}. {2}" -f $mark, ($i + 1), $title) `
                    -ForegroundColor $ColMain
            }
        }
        Write-Host "---------------------------------"
        Write-Host "  цифра — включить или выключить"
        Write-Host "  [B] назад"
        $k = (Read-Host "Выбор").ToUpper()

        if ($k -eq "B") {
            if ($changed) {
                Show-RestartHint
                Pause-Menu
            }
            break
        }
        if ($k -match '^\d+$') {
            $n = [int]$k
            if ($n -ge 1 -and $n -le $extra.Count) {
                $name = $extra[$n - 1]
                if ($active -contains $name) {
                    $active = @($active | Where-Object { $_ -ne $name })
                    Set-ActiveConfigs $active
                    $changed = $true
                } else {
                    $candidate = @($active) + $name
                    $res = Test-ConfigSet $candidate
                    if ($res.Ok) {
                        Set-ActiveConfigs $candidate
                        $changed = $true
                    } else {
                        Write-Host ""
                        Write-Host ("  Конфиг {0} содержит ошибку" -f `
                            (Get-ConfigTitle $name)) -ForegroundColor $ColAccent
                        Write-Host "  и не был включён." -ForegroundColor $ColAccent
                        $friendly = ConvertTo-FriendlyError $res.Output
                        if ($friendly) {
                            Write-Host ("  {0}" -f $friendly) `
                                -ForegroundColor $ColAccent
                        }
                        Write-Host "  Технические подробности:" `
                            -ForegroundColor $ColMuted
                        $res.Output | ForEach-Object {
                            Write-Host "    $_" -ForegroundColor $ColMuted
                        }
                        Pause-Menu
                    }
                }
            }
        }
    }
}

# ---- zapret memo ------------------------------------------------------
function Show-ZapretMemo {
    Clear-Host
    Write-Host "===== СОВМЕСТИМОСТЬ С ZAPRET =====" -ForegroundColor $ColFrame
    Write-Host ""
    Write-Host "  Если вы пользуетесь zapret (flowseal/"
    Write-Host "  zapret-discord-youtube), он может ломать трафик"
    Write-Host "  к нашему VPN-серверу. Чтобы этого не было, адрес"
    Write-Host "  сервера нужно внести в список исключений zapret."
    Write-Host ""
    Write-Host "  IP-адреса вашего сервера:" -ForegroundColor $ColAccent

    $profiles = @(Get-RealProfiles)
    if ($profiles.Count -eq 0) {
        Write-Host "    профиль не настроен — сначала [4]" `
            -ForegroundColor $ColMuted
    } else {
        $srv = @($profiles | ForEach-Object { $_.Server } | Sort-Object -Unique)
        foreach ($h in $srv) {
            if (Test-IsIp $h) {
                Write-Host ("    {0}" -f $h) -ForegroundColor $ColOn
                continue
            }
            try {
                $ips = [System.Net.Dns]::GetHostAddresses($h) |
                    Where-Object { $_.AddressFamily -eq 'InterNetwork' } |
                    ForEach-Object { $_.IPAddressToString }
                foreach ($ip in $ips) {
                    Write-Host ("    {0}    ({1})" -f $ip, $h) -ForegroundColor $ColOn
                }
            } catch {
                Write-Host ("    не удалось определить IP для {0}" -f $h) `
                    -ForegroundColor $ColAccent
            }
        }
    }

    Write-Host ""
    Write-Host "  Куда вписать:" -ForegroundColor $ColAccent
    Write-Host "    сборка Flowseal — файл lists\ipset-exclude.txt"
    Write-Host "    по одному адресу в строке, затем перезапустить zapret"
    Write-Host ""
    Write-Host "  Важно:" -ForegroundColor $ColAccent
    Write-Host "    Исключения работают только в стратегиях, где в"
    Write-Host "    параметрах запуска есть --ipset-exclude."
    Write-Host ""
    Write-Host "  IP сервера может смениться — если VPN перестал"
    Write-Host "  подключаться, загляните сюда снова."
    Write-Host "=================================="
    Pause-Menu
}

# ---- status check -----------------------------------------------------
function Show-Status {
    Clear-Host
    Write-Host "===== ПРОВЕРКА СОСТОЯНИЯ =====" -ForegroundColor $ColFrame
    Write-Host ""

    $p = Get-SbProc
    if ($p) {
        Write-Host ("  [OK]  VPN запущен (процесс {0})" -f $p.Id) `
            -ForegroundColor $ColOn
    } else {
        Write-Host "  [--]  VPN выключен" -ForegroundColor $ColAccent
    }

    $noProfile = (@(Get-RealProfiles).Count -eq 0)
    if (Test-Path $SbExe) {
        $chk = & $SbExe check @(Get-SbConfigArgs) 2>&1
        if ($LASTEXITCODE -eq 0) {
            Write-Host "  [OK]  Настройки корректны" -ForegroundColor $ColOn
        } elseif ($noProfile) {
            # fresh install: config.json is the template with dummy
            # credentials, so "check" fails on purpose. This is not an
            # error to fix - the user simply has not imported a profile.
            Write-Host "  [--]  Профиль ещё не импортирован" -ForegroundColor $ColAccent
            Write-Host "        Откройте [4] и вставьте ссылку," `
                -ForegroundColor $ColMuted
            Write-Host "        которую вам дал владелец сервера." `
                -ForegroundColor $ColMuted
        } else {
            Write-Host "  [!!]  В настройках ошибка" -ForegroundColor $ColAccent
            $friendly = ConvertTo-FriendlyError $chk
            if ($friendly) {
                Write-Host ("        {0}" -f $friendly) -ForegroundColor $ColAccent
            }
            $chk | ForEach-Object {
                Write-Host "        $_" -ForegroundColor $ColMuted
            }
        }
    } else {
        Write-Host "  [!!]  Не найден sing-box.exe" -ForegroundColor $ColAccent
    }

    $profiles = @(Get-RealProfiles)
    $act = Get-ActiveProfile
    if ($profiles.Count -eq 0) {
        Write-Host "  [!!]  Профиль не настроен — [4]" -ForegroundColor $ColAccent
    } else {
        foreach ($pr in $profiles) {
            $suffix = ""
            if ($pr.Tag -ne $act) {
                $suffix = "   (не активен)"
            }
            $label = "{0}  {1}:{2}{3}" -f $pr.Type, $pr.Server, $pr.Port, $suffix
            if ($pr.Type -eq "hysteria2") {
                $resolved = $false
                if (-not (Test-IsIp $pr.Server)) {
                    try {
                        $addr = [System.Net.Dns]::GetHostAddresses($pr.Server)
                        if ($addr.Count -gt 0) { $resolved = $true }
                    } catch { $resolved = $false }
                }
                if ((Test-IsIp $pr.Server) -or $resolved) {
                    Write-Host ("  [OK]  Адрес определяется: {0}" -f $label) `
                        -ForegroundColor $ColOn
                } else {
                    Write-Host ("  [!!]  Адрес не определяется: {0}" -f $label) `
                        -ForegroundColor $ColAccent
                }
                Write-Host "        hysteria2 работает по UDP — доступность" `
                    -ForegroundColor $ColMuted
                Write-Host "        проверяется только подключением ([1])." `
                    -ForegroundColor $ColMuted
            } else {
                if (Test-ServerReachable $pr.Server $pr.Port) {
                    Write-Host ("  [OK]  Порт открыт: {0}" -f $label) `
                        -ForegroundColor $ColOn
                } else {
                    Write-Host ("  [!!]  Порт закрыт: {0}" -f $label) `
                        -ForegroundColor $ColAccent
                }
            }
        }
    }

    Write-Host ""
    Write-Host ("  Список адресов : {0}" -f (Get-ListCount)) -ForegroundColor $ColMain
    Write-Host "=============================="
    Pause-Menu
}

# ---- advanced submenu -------------------------------------------------
function Menu-Advanced {
    while ($true) {
        Clear-Host
        Write-Host "===== ДОПОЛНИТЕЛЬНО =====" -ForegroundColor $ColFrame
        Write-Host "  [1] Пересобрать список"
        Write-Host "  [2] Показать журнал sing-box"
        Write-Host "  [B] Назад"
        Write-Host "========================="
        $k = (Read-Host "Выбор").ToUpper()

        if ($k -eq "B") {
            break
        }
        elseif ($k -eq "1") {
            & (Join-Path $BinDir "build-lists.ps1")
            Show-ListApplied
            Pause-Menu
        }
        elseif ($k -eq "2") {
            if (Test-Path $LogFile) {
                Get-Content $LogFile -Tail 30
            } else {
                Write-Host "  Журнал пока пуст." -ForegroundColor $ColAccent
            }
            Pause-Menu
        }
    }
}

# ---- edit routing list ------------------------------------------------
function Edit-GeneralList {
    Clear-Host
    Write-Host "Сейчас откроется Блокнот со списком адресов." `
        -ForegroundColor $ColFrame
    Write-Host "Впишите нужные сайты по одному в строке," -ForegroundColor $ColMuted
    Write-Host "сохраните (Ctrl+S) и закройте Блокнот." -ForegroundColor $ColMuted
    Start-Sleep -Milliseconds 1200
    Start-Process notepad.exe -ArgumentList "`"$GeneralTxt`"" -Wait
    & (Join-Path $BinDir "build-lists.ps1")
    Show-ListApplied
    Pause-Menu
}

# ---- startup ----------------------------------------------------------
Clear-Host
if (-not (Test-Path $SbExe)) {
    Write-Host "Первый запуск: скачиваю sing-box..." -ForegroundColor $ColFrame
    $ok = Ensure-SingBox
    if (-not $ok) {
        Write-Host ""
        Write-Host "  Не удалось скачать sing-box." -ForegroundColor $ColAccent
        Write-Host "  Проверьте интернет и запустите ещё раз," `
            -ForegroundColor $ColAccent
        Write-Host "  либо положите sing-box.exe в папку sing-box\ вручную." `
            -ForegroundColor $ColMuted
        Write-Host "  Возможная причина: GitHub недоступен из вашей сети." `
            -ForegroundColor $ColMuted
        Pause-Menu
    }
}
Write-Host "Собираю список адресов..." -ForegroundColor $ColFrame
& (Join-Path $BinDir "build-lists.ps1")

if (Convert-LegacyGroup) {
    Write-Host ""
    Write-Host "  Настройки обновлены: выбор профиля теперь" `
        -ForegroundColor $ColAccent
    Write-Host "  задаётся вручную в пункте [4]." -ForegroundColor $ColAccent
}
Start-Sleep -Milliseconds 800

if ((Get-VpnWanted) -and (-not (Get-SbProc))) {
    if (Test-WantedThisBoot) {
        Show-CrashAlert "VPN остановился сам, без вашей команды."
    }
    Set-VpnWanted $false
}

# ---- main loop --------------------------------------------------------
while ($true) {
    $sb = [bool](Get-SbProc)

    if ((Get-VpnWanted) -and (-not $sb)) {
        if (Test-WantedThisBoot) {
            Show-CrashAlert "VPN остановился сам, без вашей команды."
            $script:CrashNotice = $true
        }
        Set-VpnWanted $false
        continue
    }

    Clear-Host
    Write-Host ("  UTGARD v{0}" -f (Get-ClientVersion)) -ForegroundColor $ColFrame
    Write-Host "  ======================================" -ForegroundColor $ColFrame
    Write-Host "  VPN: " -NoNewline -ForegroundColor $ColMain
    if ($sb) {
        Write-Host "Включён" -ForegroundColor $ColOn
    } else {
        Write-Host "Выключен" -ForegroundColor $ColOff
    }
    if ($script:CrashNotice -and -not $sb) {
        Write-Host "       VPN упал — трафик идёт напрямую" -ForegroundColor $ColAccent
    }
    Show-ProfileBlock
    Write-Host ("  Конфигурация: {0}" -f (Get-ConfigLine)) -ForegroundColor $ColMain
    $cnt = Get-ListCount
    $stamp = Get-ListStamp
    if ($stamp) {
        Write-Host ("  Список сайтов: {0} загружен, обновление {1}" -f `
            $cnt, $stamp) -ForegroundColor $ColMuted
    } else {
        Write-Host ("  Список сайтов: {0} загружен" -f $cnt) -ForegroundColor $ColMuted
    }
    Write-Host "  ======================================" -ForegroundColor $ColFrame
    Write-Host "  [1] Включить VPN"
    Write-Host "  [2] Выключить VPN"
    Write-Host "  [3] Список сайтов, которые идут через VPN"
    Write-Host "  [4] Профили VPN"
    Write-Host "  [5] Изменение конфигурации"
    Write-Host "  [6] Совместимость с flowseal/zapret-discord-youtube"
    Write-Host "  [7] Проверить состояние"
    Write-Host "  [8] Дополнительно"
    Write-Host "  [Q] Выход (выключает VPN)"
    Write-Host "  ======================================" -ForegroundColor $ColFrame

    switch ((Read-Host "Выбор").ToUpper()) {
        "1" {
            if (Get-SbProc) {
                Write-Host "  VPN уже включён." -ForegroundColor $ColAccent
                Pause-Menu
            } else {
                $ready = Test-ProfileReady
                if (-not $ready.Ok) {
                    Write-Host ""
                    if ($ready.Reason -eq "none") {
                        Write-Host "  Профиль подключения не настроен." `
                            -ForegroundColor $ColAccent
                        Write-Host "  Откройте [4] и вставьте ссылку." `
                            -ForegroundColor $ColAccent
                    } else {
                        Write-Host "  Профилей несколько — выберите активный" `
                            -ForegroundColor $ColAccent
                        Write-Host "  в пункте [4]." -ForegroundColor $ColAccent
                    }
                    Pause-Menu
                } elseif (Start-Sb) {
                    Set-VpnWanted $true
                    $script:CrashNotice = $false
                } else {
                    Pause-Menu
                }
            }
        }
        "2" {
            Set-VpnWanted $false
            $script:CrashNotice = $false
            Stop-Sb | Out-Null
        }
        "3" { Edit-GeneralList }
        "4" { Menu-Profile }
        "5" { Menu-Configs }
        "6" { Show-ZapretMemo }
        "7" { Show-Status }
        "8" { Menu-Advanced }
        "Q" {
            Set-VpnWanted $false
            Stop-Sb | Out-Null
            exit 0
        }
    }
}