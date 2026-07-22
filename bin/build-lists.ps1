# build-lists.ps1 - compile list-general.txt into a sing-box
# rule-set (general.srs). The intermediate general.json is only
# input for the compiler: it is removed once compilation succeeds,
# and deliberately KEPT when it fails, so the bad output can be
# inspected.
# ENCODING: this file must be saved as UTF-8 with BOM.

. (Join-Path (Split-Path $PSCommandPath -Parent) "common.ps1")
Ensure-Dirs

$all = Read-DomainFile $GeneralTxt

# split IPs/CIDRs from domains
$ips = @($all |
    Where-Object { Test-IsIp $_ } |
    ForEach-Object {
        if ($_ -notmatch '/') { "$_/32" } else { $_ }
    } |
    Sort-Object -Unique)

$domains = @($all |
    Where-Object { -not (Test-IsIp $_) } |
    Sort-Object -Unique)

# collapse subdomains already covered by a parent domain_suffix
$set = @{}
foreach ($d in $domains) {
    $set[$d] = $true
}

$result = New-Object System.Collections.Generic.List[string]
foreach ($d in $domains) {
    $covered = $false
    $parts = $d -split '\.'
    for ($i = 1; $i -lt ($parts.Count - 1); $i++) {
        $parent = ($parts[$i..($parts.Count - 1)]) -join '.'
        if ($set.ContainsKey($parent)) {
            $covered = $true
            break
        }
    }
    if (-not $covered) {
        $result.Add($d)
    }
}

$isEmpty = ($result.Count -eq 0 -and $ips.Count -eq 0)

$rule = @{}
if ($result.Count) {
    $rule.domain_suffix = @($result)
}
if ($ips.Count) {
    $rule.ip_cidr = @($ips)
}
if ($isEmpty) {
    # sing-box rejects an empty rule; emit a harmless placeholder
    $rule.domain_suffix = @("invalid.placeholder.local")
}

$doc = @{ version = 3; rules = @($rule) } | ConvertTo-Json -Depth 6
Write-Utf8 $GeneralJson $doc

if (-not (Test-Path $SbExe)) {
    Write-Host ""
    Write-Host "  Не найден sing-box.exe — список подготовлен," `
        -ForegroundColor Yellow
    Write-Host "  но собрать его не удалось." -ForegroundColor Yellow
    return
}

$compileOut = & $SbExe rule-set compile $GeneralJson -o $GeneralSrs 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "  Не удалось собрать список." -ForegroundColor Red
    Write-Host "  Проверьте, нет ли в файле лишних символов." `
        -ForegroundColor Yellow
    Write-Host "  Технические подробности:" -ForegroundColor DarkGray
    $compileOut | ForEach-Object {
        Write-Host "    $_" -ForegroundColor DarkGray
    }
    # general.json is kept on purpose here - it is the artefact
    # that failed and the only way to see what went wrong.
    return
}

# compilation succeeded: the intermediate file is no longer needed
Remove-Item $GeneralJson -Force -ErrorAction SilentlyContinue

$stamp = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")
Write-Utf8 $BuildStampFile $stamp

$collapsed = $domains.Count - $result.Count

Write-Host ""
if ($isEmpty) {
    Write-Host "  Список пуст — через VPN ничего не пойдёт." `
        -ForegroundColor Yellow
    Write-Host "  Добавьте нужные сайты в пункте [3]." `
        -ForegroundColor Yellow
} else {
    Write-Host ("  адресов  : {0}" -f $result.Count) -ForegroundColor Green
    if ($ips.Count) {
        Write-Host ("  подсетей : {0}" -f $ips.Count) -ForegroundColor Green
    }
    if ($collapsed -gt 0) {
        Write-Host ("  свёрнуто : {0} лишних поддоменов" -f $collapsed) `
            -ForegroundColor DarkGray
    }
}
Write-Host ("  собрано  : {0}" -f $stamp) -ForegroundColor DarkGray