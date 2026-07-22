# reset-profiles.ps1 - remove imported profiles from config.json,
# one by one or all at once. When the last one goes, the shipped
# templates from config.default.json are restored, so the result
# matches a fresh install.
# A full reset also drops config.last-good.json: leaving deleted
# credentials in a recovery snapshot would defeat the point.
# ENCODING: this file must be saved as UTF-8 with BOM.

. (Join-Path (Split-Path $PSCommandPath -Parent) "common.ps1")

# --- main -----------------------------------------------------------
Clear-Host
Write-Host "===== ОЧИСТКА ПРОФИЛЕЙ =====" -ForegroundColor Cyan
Write-Host ""

$profiles = @(Get-ProxyOutbounds | Where-Object { -not $_.Placeholder })

if ($profiles.Count -eq 0) {
    Write-Host "  Импортированных профилей нет —" -ForegroundColor Yellow
    Write-Host "  очищать нечего." -ForegroundColor Yellow
    return
}

Write-Host "  Сейчас настроены:" -ForegroundColor Gray
for ($i = 0; $i -lt $profiles.Count; $i++) {
    Write-Host ("   {0}. {1} — {2}" -f `
        ($i + 1), $profiles[$i].Type, $profiles[$i].Server)
}
Write-Host ""
Write-Host "  цифра — удалить один профиль"
Write-Host "  [A]    — удалить все"
Write-Host "  [B]    — отмена"
$k = (Read-Host "Выбор").ToUpper()

$victims = @()
if ($k -eq "B" -or $k -eq "") {
    Write-Host "  Отменено." -ForegroundColor Yellow
    return
}
elseif ($k -eq "A") {
    $victims = @($profiles | ForEach-Object { $_.Tag })
}
elseif ($k -match '^\d+$') {
    $n = [int]$k
    if ($n -lt 1 -or $n -gt $profiles.Count) {
        Write-Host "  Такого номера нет." -ForegroundColor Yellow
        return
    }
    $victims = @($profiles[$n - 1].Tag)
}
else {
    Write-Host "  Не понял выбор." -ForegroundColor Yellow
    return
}

Write-Host ""
Write-Host "  Будут удалены настройки подключения." -ForegroundColor Yellow
Write-Host "  Чтобы пользоваться VPN снова, понадобится" `
    -ForegroundColor Yellow
Write-Host "  заново вставить ссылку в пункте [4]." -ForegroundColor Yellow
Write-Host ""
Write-Host "  [1] удалить"
Write-Host "  [0] отмена"
$confirm = (Read-Host "Выбор").Trim()
if ($confirm -ne "1") {
    Write-Host "  Отменено." -ForegroundColor Yellow
    return
}

$read = Read-BaseConfig
if ($null -eq $read.Cfg) {
    Write-Host ""
    Write-Host "  Файл настроек повреждён, и восстановить его" `
        -ForegroundColor Red
    Write-Host "  не из чего. Очистка отменена." -ForegroundColor Red
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

# 1) drop the chosen outbounds
$kept = @()
foreach ($o in $cfg.outbounds) {
    if ($victims -contains $o.tag) {
        continue
    }
    $kept += $o
}

# 2) nothing proxy-like left? restore the shipped templates
$remaining = @($kept | Where-Object { $ProxyTypes -contains $_.type })
$restored = $false
if ($remaining.Count -eq 0) {
    $tpl = @(Get-TemplateOutbounds)
    if ($tpl.Count -eq 0) {
        Write-Host ""
        Write-Host "  Не найден config.default.json —" -ForegroundColor Red
        Write-Host "  без него нельзя восстановить заготовки." `
            -ForegroundColor Red
        Write-Host "  Очистка отменена, ничего не изменено." `
            -ForegroundColor Red
        Remove-ConfigBackup
        return
    }
    # rebuild in the shipped order: direct, profiles, group.
    # the group is a selector now; "urltest" is only kept in the
    # filter so that configs written by older versions land right.
    $groupTypes = @('selector', 'urltest')
    $others = @($kept | Where-Object { $groupTypes -notcontains $_.type })
    $groups = @($kept | Where-Object { $groupTypes -contains $_.type })
    $kept = @($others) + @($tpl) + @($groups)
    $restored = $true
}
$cfg.outbounds = $kept

# 3) profile group + bypass rules, same helper the import uses
Sync-ProxyConfig $cfg | Out-Null

Write-JsonConfig $BaseConfig $cfg

# 4) validate - but only while a real profile is still present.
# After a full reset the templates intentionally fail the check,
# exactly as they do on a fresh install.
if ((Test-Path $SbExe) -and (-not $restored)) {
    $chk = & $SbExe check -c $BaseConfig 2>&1
    if ($LASTEXITCODE -ne 0) {
        Restore-ConfigBackup
        Write-Host ""
        Write-Host "  Не удалось применить изменения," -ForegroundColor Red
        Write-Host "  настройки возвращены как были." -ForegroundColor Red
        Write-Host "  Технические подробности:" -ForegroundColor DarkGray
        $chk | ForEach-Object {
            Write-Host "    $_" -ForegroundColor DarkGray
        }
        return
    }
    Save-LastGood
}

if ($restored) {
    # no profiles left: the recovery snapshot would still hold the
    # credentials the user just asked to delete
    if (Test-Path $LastGoodConfig) {
        Remove-Item $LastGoodConfig -Force -ErrorAction SilentlyContinue
    }
}

Remove-ConfigBackup

Write-Host ""
Write-Host "  Готово." -ForegroundColor Green
if ($restored) {
    Write-Host "  Все профили удалены. Настройки вернулись" `
        -ForegroundColor Gray
    Write-Host "  к состоянию свежей установки." -ForegroundColor Gray
    Write-Host ""
    Write-Host "  Чтобы пользоваться VPN, импортируйте" -ForegroundColor Yellow
    Write-Host "  ссылку в пункте [4]." -ForegroundColor Yellow
} else {
    $left = @(Get-ProxyOutbounds | Where-Object { -not $_.Placeholder })
    Write-Host ("  Осталось профилей: {0}" -f $left.Count) -ForegroundColor Gray
    foreach ($p in $left) {
        Write-Host ("   {0} — {1}" -f $p.Type, $p.Server) -ForegroundColor Gray
    }
}
Write-Host ""
Write-Host "  Если VPN сейчас включён, перезапустите его:" `
    -ForegroundColor Yellow
Write-Host "  [2], затем [1]." -ForegroundColor Yellow