#include "awgcore.h"
#include "coredir.h"
#include "net.h"

#include <windows.h>
#include <strsafe.h>

static int say(wchar_t *msg, size_t cap, const wchar_t *text)
{
    if (msg && cap) StringCchCopyW(msg, cap, text);
    return 0;
}

int awgcore_present(void)
{
    coredir_hold h;
    if (!coredir_hold_verified(&CORE_AWG, &h, NULL, 0, NULL)) return 0;
    coredir_release(&h);
    return 1;
}

int awgcore_install(wchar_t *msg, size_t cap)
{
    wchar_t dir[MAX_PATH * 2], work[MAX_PATH * 2], msi[MAX_PATH * 2], unpack[MAX_PATH * 2];
    wchar_t from[MAX_PATH * 2], to[MAX_PATH * 2], sys[MAX_PATH], cmd[2048], hex[80];
    HANDLE  held = INVALID_HANDLE_VALUE;
    DWORD   code = 1;
    int     i, ok = 0;

    if (msg && cap) msg[0] = L'\0';
    if (!coredir_path(&CORE_AWG, dir, MAX_PATH * 2) ||
        FAILED(StringCchPrintfW(work, MAX_PATH * 2, L"%s\\install.tmp", dir)) ||
        FAILED(StringCchPrintfW(msi, MAX_PATH * 2, L"%s\\core.msi", work)) ||
        FAILED(StringCchPrintfW(unpack, MAX_PATH * 2, L"%s\\x", work)))
        return say(msg, cap, L"Не удалось определить папку AmneziaWG");

    /* The core runs as SYSTEM: only from a folder we can close to others. */
    if (!coredir_volume_has_acl(dir))
        return say(msg, cap, L"AmneziaWG недоступен: папка программы на диске без прав доступа "
                             L"(FAT32 или exFAT). Перенесите Utgard на диск NTFS.");
    if (!coredir_protect(&CORE_AWG, msg, cap)) return 0;

    /* Inside the protected folder, as for sing-box: nothing another program
       of the user could reach between our check and the copy. */
    coredir_wipe(work);
    if (!CreateDirectoryW(work, NULL))
        return say(msg, cap, L"Не удалось создать рабочую папку загрузки");

    if (!net_download(CORE_AWG.url, msi, msg, cap)) goto cleanup;

    /* Hashed through a handle kept open until msiexec is done: the package
       it unpacks is the one that was checked. */
    held = CreateFileW(msi, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (held == INVALID_HANDLE_VALUE) { say(msg, cap, L"Не удалось открыть загруженный пакет");
                                        goto cleanup; }
    if (!coredir_sha256_handle(held, hex, 80)) { say(msg, cap, L"Не удалось посчитать контрольную сумму");
                                                 goto cleanup; }
    if (_wcsicmp(hex, CORE_AWG.archive_sha256) != 0) {
        if (msg && cap)
            StringCchPrintfW(msg, cap, L"Контрольная сумма пакета AmneziaWG не совпала. "
                             L"Ожидалась %s, получена %s. Файл повреждён или подменён.",
                             CORE_AWG.archive_sha256, hex);
        goto cleanup;
    }

    /* An administrative image: Windows Installer only copies the files out.
       The package's AdminExecuteSequence has no custom actions - checked
       for both architectures - so nothing is registered and no service or
       adapter is touched. By full path, as tar.exe for sing-box. */
    if (!GetSystemDirectoryW(sys, MAX_PATH)) { say(msg, cap, L"Нет доступа к System32");
                                               goto cleanup; }
    if (FAILED(StringCchPrintfW(cmd, 2048, L"\"%s\\msiexec.exe\" /a \"%s\" /qn TARGETDIR=\"%s\"",
                                sys, msi, unpack))) {
        say(msg, cap, L"Слишком длинный путь");
        goto cleanup;
    }
    if (!coredir_run_wait(cmd, 180000, &code) || code != 0) {
        if (code == 1618)       /* ERROR_INSTALL_ALREADY_RUNNING */
            say(msg, cap, L"Сейчас идёт другая установка программ. Дождитесь её окончания "
                          L"и попробуйте снова.");
        else if (msg && cap)
            StringCchPrintfW(msg, cap, L"Не удалось распаковать пакет AmneziaWG (код %lu)",
                             (unsigned long)code);
        goto cleanup;
    }

    /* The package puts its files under TARGETDIR\AmneziaWG (its Directory
       table). Only the core's own files are taken, by name. */
    for (i = 0; i < CORE_AWG.nfiles; i++) {
        if (FAILED(StringCchPrintfW(from, MAX_PATH * 2, L"%s\\AmneziaWG\\%s", unpack, CORE_AWG.files[i].name)) ||
            FAILED(StringCchPrintfW(to, MAX_PATH * 2, L"%s\\%s", dir, CORE_AWG.files[i].name)) ||
            !CopyFileW(from, to, FALSE)) {
            if (msg && cap)
                StringCchPrintfW(msg, cap, L"В пакете AmneziaWG не нашлось %s", CORE_AWG.files[i].name);
            goto cleanup;
        }
    }
    ok = 1;

cleanup:
    if (held != INVALID_HANDLE_VALUE) CloseHandle(held);
    coredir_wipe(work);
    if (ok) {
        coredir_hold h;
        /* Copied files take the folder's permissions; set them explicitly
           anyway, then check exactly as every start will. */
        coredir_protect(&CORE_AWG, NULL, 0);
        ok = coredir_hold_verified(&CORE_AWG, &h, msg, cap, NULL);
        if (ok) coredir_release(&h);
    }
    return ok;
}
