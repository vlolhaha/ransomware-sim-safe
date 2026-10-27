/*
 * safe_sim.c — SAFE Ransomware Simulator (Win32 C) [v2]
 * -----------------------------------------------------
 * Trông nguy hiểm. Chạy an toàn. Không vi phạm pháp luật.
 *
 * Điểm mới v2:
 *   - WM_CLOSE → MessageBox xác nhận (không thoát ngay, nhưng không chặn cứng)
 *   - atexit(restore_wallpaper) + SEH __try/__finally → wallpaper luôn revert
 *   - Thêm chi tiết thị giác "nguy hiểm" (warning bar, glitch text, red pulse)
 *
 * Build (MSVC):
 *   cl safe_sim.c /link user32.lib gdi32.lib shell32.lib advapi32.lib
 *
 * Build (MinGW):
 *   gcc safe_sim.c -o safe_sim.exe -mwindows -luser32 -lgdi32 -lshell32 -ladvapi32
 *
 * Thoát: EXIT button / Esc / Alt+F4 (có xác nhận).
 */

#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================
 * CẤU HÌNH
 * ============================================================ */

#define ENCRYPTED_EXT   L".locked"
#define NOTE_NAME       L"README_RESTORE_FILES.txt"
#define FAKE_KEY        L"A7F3-DEMO-9X2Q-4L8M-6K1P-3R5T"
#define DECOY_BTC       L"bc1qsim0demo0only0notreal0xxxxxxxxxxxxxxxx"
#define COUNTDOWN_SECS  (72 * 3600)

static const wchar_t *TARGET_EXTS[] = {
    L".txt", L".docx", L".pdf", L".jpg", L".jpeg", L".png",
    L".xlsx", L".pptx", L".mp4", L".mov", L".zip", L".rar",
    L".csv", L".md", NULL
};

/* ============================================================
 * STATE
 * ============================================================ */

static HWND    g_hwnd_main    = NULL;
static HWND    g_hwnd_log     = NULL;
static HWND    g_hwnd_timer   = NULL;
static HWND    g_hwnd_fakekill= NULL;
static HWND    g_hwnd_glitch  = NULL;
static time_t  g_start_time   = 0;
static volatile LONG g_fake_kill_count = 0;
static volatile LONG g_running = 1;
static volatile LONG g_atexit_registered = 0;
static wchar_t g_sandbox[MAX_PATH];
static wchar_t g_cache[MAX_PATH];
static wchar_t g_old_wallpaper[MAX_PATH] = { 0 };
static int     g_wallpaper_saved = 0;

static const wchar_t *FAKE_PROCS[] = {
    L"chrome.exe", L"discord.exe", L"notepad.exe", L"code.exe",
    L"spotify.exe", L"steam.exe", L"excel.exe", L"outlook.exe",
    L"telegram.exe", L"firefox.exe", L"slack.exe", L"vlc.exe",
    NULL
};

static const wchar_t *GLITCH_TEXT[] = {
    L"ENCRYPTING...", L"LOCKING FILES...", L"KEY GENERATED",
    L"PRIVATE KEY SECURED", L"BACKUP DELETED",
    L"SHADOW COPY REMOVED", L"NETWORK SCAN ACTIVE",
    L"UPLOADING KEY...", L"PAYMENT DEADLINE SET",
    NULL
};

/* ============================================================
 * UTIL
 * ============================================================ */

static void get_sandbox_path(void) {
    wchar_t profile[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, profile))) {
        _snwprintf(g_sandbox, MAX_PATH, L"%ls\\RansomwareSimSandbox", profile);
    } else {
        wcscpy(g_sandbox, L"C:\\RansomwareSimSandbox");
    }
    _snwprintf(g_cache, MAX_PATH, L"%ls\\wallpaper_sim.bmp", g_sandbox);
}

static int ext_matches(const wchar_t *path) {
    const wchar_t *dot = wcsrchr(path, L'.');
    if (!dot) return 0;
    for (int i = 0; TARGET_EXTS[i]; i++)
        if (_wcsicmp(dot, TARGET_EXTS[i]) == 0) return 1;
    return 0;
}

static void log_line(const wchar_t *fmt, ...) {
    if (!g_hwnd_log) return;
    wchar_t buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(buf, 1024, fmt, ap);
    va_end(ap);
    int len = GetWindowTextLengthW(g_hwnd_log);
    SendMessageW(g_hwnd_log, EM_SETSEL, len, len);
    SendMessageW(g_hwnd_log, EM_REPLACESEL, FALSE, (LPARAM)buf);
    SendMessageW(g_hwnd_log, EM_REPLACESEL, FALSE, (LPARAM)L"\r\n");
    SendMessageW(g_hwnd_log, EM_SCROLLCARET, 0, 0);
}

/* ============================================================
 * WALLPAPER — save / restore / atexit
 * ============================================================ */

static void save_old_wallpaper(void) {
    HKEY hk;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Control Panel\\Desktop", 0, KEY_READ, &hk) == ERROR_SUCCESS) {
        DWORD size = sizeof(g_old_wallpaper);
        DWORD type = 0;
        if (RegQueryValueExW(hk, L"Wallpaper", NULL, &type,
                (LPBYTE)g_old_wallpaper, &size) == ERROR_SUCCESS) {
            g_wallpaper_saved = 1;
        }
        RegCloseKey(hk);
    }
}

static void restore_wallpaper(void) {
    if (g_wallpaper_saved && g_old_wallpaper[0]) {
        SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, g_old_wallpaper,
                              SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    }
}

static void register_wallpaper_safety(void) {
    if (InterlockedExchange(&g_atexit_registered, 1) == 0) {
        atexit(restore_wallpaper);
    }
}

/* ============================================================
 * SANDBOX + ENCRYPT
 * ============================================================ */

static void make_dir_recursive(const wchar_t *path) {
    wchar_t tmp[MAX_PATH];
    wcscpy(tmp, path);
    for (wchar_t *p = tmp + 1; *p; p++) {
        if (*p == L'\\') {
            *p = 0;
            CreateDirectoryW(tmp, NULL);
            *p = L'\\';
        }
    }
    CreateDirectoryW(tmp, NULL);
}

static void write_decoy_file(const wchar_t *path) {
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    const char *c = "SIMULATED CONTENT — not real user data.\r\n";
    for (int i = 0; i < 20; i++) {
        DWORD w;
        WriteFile(h, c, (DWORD)strlen(c), &w, NULL);
    }
    CloseHandle(h);
}

static void build_sandbox(void) {
    wchar_t cmd[MAX_PATH * 2];
    _snwprintf(cmd, MAX_PATH * 2, L"cmd /c rmdir /s /q \"%ls\" 2>nul", g_sandbox);
    _wsystem(cmd);

    const wchar_t *folders[] = {
        L"Documents\\Work", L"Documents\\Personal", L"Pictures\\2024",
        L"Pictures\\Vacation", L"Desktop", L"Downloads", L"Music",
        L"Videos", L"Backups", NULL
    };
    const wchar_t *names[] = {
        L"report_q4", L"invoice_2024_03", L"budget", L"notes_meeting",
        L"family_trip", L"screenshot", L"IMG_20240311", L"resume",
        L"contract_final", L"passwords", L"todo", L"presentation",
        L"dataset", L"export", L"backup_db", L"letter", NULL
    };

    for (int i = 0; folders[i]; i++) {
        wchar_t dir[MAX_PATH];
        _snwprintf(dir, MAX_PATH, L"%ls\\%ls", g_sandbox, folders[i]);
        make_dir_recursive(dir);
        int n = 4 + rand() % 5;
        for (int j = 0; j < n; j++) {
            const wchar_t *base = names[rand() % 16];
            const wchar_t *ext  = TARGET_EXTS[rand() % 13];
            wchar_t fname[MAX_PATH];
            _snwprintf(fname, MAX_PATH, L"%ls\\%ls_%04x%ls",
                       dir, base, rand() & 0xffff, ext);
            write_decoy_file(fname);
        }
    }
}

static int encrypt_dir(const wchar_t *dir) {
    wchar_t pattern[MAX_PATH];
    _snwprintf(pattern, MAX_PATH, L"%ls\\*", dir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;

    int count = 0;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        wchar_t full[MAX_PATH];
        _snwprintf(full, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            count += encrypt_dir(full);
        } else if (ext_matches(full)) {
            wchar_t cipher[33];
            for (int i = 0; i < 32; i++)
                cipher[i] = L"0123456789abcdef"[rand() % 16];
            cipher[32] = 0;
            wchar_t newname[MAX_PATH];
            _snwprintf(newname, MAX_PATH, L"%ls%ls", full, ENCRYPTED_EXT);
            if (MoveFileW(full, newname)) {
                log_line(L"[ENCRYPTED] %ls -> %ls", full, cipher);
                count++;
                Sleep(8);
            }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return count;
}

/* ============================================================
 * WALLPAPER DRAW
 * ============================================================ */

static HBITMAP create_wallpaper_bitmap(int w, int h) {
    HDC hdc = GetDC(NULL);
    HDC memdc = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
    HBITMAP old = (HBITMAP)SelectObject(memdc, bmp);

    RECT rc = { 0, 0, w, h };
    HBRUSH black = CreateSolidBrush(RGB(3, 3, 3));
    FillRect(memdc, &rc, black);
    DeleteObject(black);

    /* vạch đỏ trên cùng — warning bar */
    RECT bar = { 0, 0, w, 12 };
    HBRUSH red = CreateSolidBrush(RGB(255, 20, 20));
    FillRect(memdc, &bar, red);
    DeleteObject(red);

    SetBkMode(memdc, TRANSPARENT);

    SetTextColor(memdc, RGB(255, 20, 20));
    HFONT f1 = CreateFontW(80, 0, 0, 0, FW_BOLD, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, 0, 0, L"Consolas");
    SelectObject(memdc, f1);
    const wchar_t *t = L"YOUR FILES HAVE BEEN ENCRYPTED";
    SIZE sz; GetTextExtentPoint32W(memdc, t, (int)wcslen(t), &sz);
    TextOutW(memdc, w/2 - sz.cx/2, 160, t, (int)wcslen(t));

    const wchar_t *t2 = L"ALL YOUR DATA IS LOCKED";
    HFONT f1b = CreateFontW(36, 0, 0, 0, FW_BOLD, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, 0, 0, L"Consolas");
    SelectObject(memdc, f1b);
    SetTextColor(memdc, RGB(255, 60, 60));
    GetTextExtentPoint32W(memdc, t2, (int)wcslen(t2), &sz);
    TextOutW(memdc, w/2 - sz.cx/2, 280, t2, (int)wcslen(t2));

    HFONT f2 = CreateFontW(26, 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, 0, 0, L"Consolas");
    SelectObject(memdc, f2);
    wchar_t k[128];
    _snwprintf(k, 128, L"Personal key: %ls", FAKE_KEY);
    SetTextColor(memdc, RGB(255, 170, 0));
    GetTextExtentPoint32W(memdc, k, (int)wcslen(k), &sz);
    TextOutW(memdc, w/2 - sz.cx/2, 370, k, (int)wcslen(k));

    SetTextColor(memdc, RGB(255, 80, 80));
    const wchar_t *lines[] = {
        L"Send 0.05 BTC to restore your files",
        DECOY_BTC,
        L"Read README_RESTORE_FILES.txt for instructions",
        NULL
    };
    for (int i = 0; lines[i]; i++) {
        GetTextExtentPoint32W(memdc, lines[i], (int)wcslen(lines[i]), &sz);
        TextOutW(memdc, w/2 - sz.cx/2, 450 + i*50,
                 lines[i], (int)wcslen(lines[i]));
    }

    /* fake "progress" bar — đỏ, trông như đang mã hoá */
    RECT pb_bg = { w/2 - 400, 650, w/2 + 400, 690 };
    HBRUSH pb_bg_br = CreateSolidBrush(RGB(40, 0, 0));
    FillRect(memdc, &pb_bg, pb_bg_br);
    DeleteObject(pb_bg_br);
    RECT pb = { w/2 - 400, 650, w/2 - 400 + 620, 690 };
    HBRUSH pb_br = CreateSolidBrush(RGB(200, 0, 0));
    FillRect(memdc, &pb, pb_br);
    DeleteObject(pb_br);

    /* footer */
    SetTextColor(memdc, RGB(0, 170, 255));
    HFONT f3 = CreateFontW(16, 0, 0, 0, FW_NORMAL, 1, 0, 0,
        DEFAULT_CHARSET, 0, 0, 0, 0, L"Consolas");
    SelectObject(memdc, f3);
    const wchar_t *d = L"DEMO SIMULATION \u2014 NO REAL DATA AFFECTED";
    GetTextExtentPoint32W(memdc, d, (int)wcslen(d), &sz);
    TextOutW(memdc, w/2 - sz.cx/2, h - 80, d, (int)wcslen(d));

    SelectObject(memdc, old);
    DeleteObject(f1); DeleteObject(f1b); DeleteObject(f2); DeleteObject(f3);
    DeleteDC(memdc); ReleaseDC(NULL, hdc);
    return bmp;
}

static void save_bitmap_to_bmp(HBITMAP bmp, const wchar_t *path) {
    BITMAP bm;
    GetObject(bmp, sizeof(bm), &bm);
    BITMAPFILEHEADER fh = { 0 };
    BITMAPINFOHEADER ih = { 0 };
    ih.biSize = sizeof(ih);
    ih.biWidth = bm.bmWidth;
    ih.biHeight = bm.bmHeight;
    ih.biPlanes = 1;
    ih.biBitCount = 24;
    ih.biCompression = BI_RGB;
    DWORD row = ((bm.bmWidth * 3 + 3) & ~3);
    DWORD img = row * bm.bmHeight;
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + img;

    HANDLE hf = CreateFileW(path, GENERIC_WRITE, 0, NULL,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf == INVALID_HANDLE_VALUE) return;
    DWORD w;
    WriteFile(hf, &fh, sizeof(fh), &w, NULL);
    WriteFile(hf, &ih, sizeof(ih), &w, NULL);

    HDC hdc = GetDC(NULL);
    HDC memdc = CreateCompatibleDC(hdc);
    SelectObject(memdc, bmp);
    BYTE *buf = (BYTE *)malloc(row);
    for (int y = bm.bmHeight - 1; y >= 0; y--) {
        for (int x = 0; x < bm.bmWidth; x++) {
            COLORREF c = GetPixel(memdc, x, y);
            buf[x*3+0] = GetBValue(c);
            buf[x*3+1] = GetGValue(c);
            buf[x*3+2] = GetRValue(c);
        }
        WriteFile(hf, buf, row, &w, NULL);
    }
    free(buf);
    DeleteDC(memdc); ReleaseDC(NULL, hdc);
    CloseHandle(hf);
}

static void set_wallpaper(const wchar_t *path) {
    SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, (PVOID)path,
                          SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
}

/* ============================================================
 * THREADS
 * ============================================================ */

static DWORD WINAPI fake_killer_thread(LPVOID p) {
    (void)p;
    log_line(L"[*] Process killer active (SIMULATION MODE).");
    log_line(L"[!] No real processes will be terminated.");
    while (g_running) {
        const wchar_t *name = FAKE_PROCS[rand() % 12];
        DWORD fake_pid = 1000 + rand() % 60000;
        InterlockedIncrement(&g_fake_kill_count);
        log_line(L"[KILLED] %ls (pid %lu) [SIMULATED]",
                 name, (unsigned long)fake_pid);
        if (g_hwnd_fakekill) {
            wchar_t buf[64];
            _snwprintf(buf, 64, L"Processes terminated (sim): %ld",
                       g_fake_kill_count);
            SetWindowTextW(g_hwnd_fakekill, buf);
        }
        Sleep(400 + rand() % 400);
    }
    return 0;
}

static DWORD WINAPI encrypt_thread(LPVOID p) {
    (void)p;
    log_line(L"[*] Initialising encryption engine...");
    log_line(L"[*] Key: %ls", FAKE_KEY);
    log_line(L"[*] Target dir: %ls", g_sandbox);
    log_line(L"");
    int n = encrypt_dir(g_sandbox);
    log_line(L"");
    log_line(L"[+] Total: %d files encrypted (SIMULATED).", n);
    log_line(L"[!] All changes confined to sandbox folder.");
    return 0;
}

static DWORD WINAPI glitch_thread(LPVOID p) {
    (void)p;
    while (g_running) {
        Sleep(1200 + rand() % 1500);
        if (!g_running || !g_hwnd_glitch) break;
        int idx = rand() % 9;
        SetWindowTextW(g_hwnd_glitch, GLITCH_TEXT[idx]);
    }
    return 0;
}

/* ============================================================
 * NOTE
 * ============================================================ */

static void drop_note(void) {
    wchar_t path[MAX_PATH];
    _snwprintf(path, MAX_PATH, L"%ls\\%ls", g_sandbox, NOTE_NAME);
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    char buf[4096];
    int n = _snprintf(buf, 4096,
        "============================================================\r\n"
        "  YOUR FILES HAVE BEEN ENCRYPTED  [SIMULATION]\r\n"
        "============================================================\r\n\r\n"
        "THIS IS A SAFE DEMO. No real files were modified.\r\n"
        "All 'encrypted' files are inside the sandbox folder only.\r\n\r\n"
        "Many of your files now have the extension: .locked\r\n\r\n"
        "HOW TO RECOVER IN THIS DEMO?\r\n"
        "Press EXIT or Esc in the simulator window.\r\n"
        "Then delete this sandbox folder to fully clean up.\r\n\r\n"
        "FAKE BTC ADDRESS (do NOT send anything):\r\n"
        "    bc1qsim0demo0only0notreal0xxxxxxxxxxxxxxxx\r\n\r\n"
        "FAKE KEY:\r\n"
        "    A7F3-DEMO-9X2Q-4L8M-6K1P-3R5T\r\n"
        "============================================================\r\n");
    DWORD w;
    WriteFile(h, buf, (DWORD)n, &w, NULL);
    CloseHandle(h);
    log_line(L"[*] Note dropped: %ls", path);
}

/* ============================================================
 * GUI
 * ============================================================ */

#define IDC_LOG        1001
#define IDC_TIMER      1002
#define IDC_DEMO       1003
#define IDC_HEADER     1004
#define IDC_KEY        1005
#define IDC_FAKEKILL   1006
#define IDC_BTC_LABEL  1007
#define IDC_BTC_ADDR   1008
#define IDC_PAY_BTN    1009
#define IDC_EXIT_BTN   1010
#define IDC_GLITCH     1011

static HFONT make_font(int size, int weight, int italic) {
    return CreateFontW(size, 0, 0, 0, weight, italic, 0, 0,
        DEFAULT_CHARSET, 0, 0, 0, 0, L"Consolas");
}

static void update_timer(HWND hwnd) {
    long elapsed = (long)(time(NULL) - g_start_time);
    long rem = COUNTDOWN_SECS - elapsed;
    if (rem < 0) rem = 0;
    int d = rem / 86400, h = (rem % 86400) / 3600;
    int m = (rem % 3600) / 60, s = rem % 60;
    wchar_t buf[128];
    _snwprintf(buf, 128, L"TIME LEFT: %dd %02d:%02d:%02d", d, h, m, s);
    SetWindowTextW(hwnd, buf);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static HBRUSH bg;
    switch (msg) {
    case WM_CREATE: {
        bg = CreateSolidBrush(RGB(3, 3, 3));
        int W = GetSystemMetrics(SM_CXSCREEN);
        int H = GetSystemMetrics(SM_CYSCREEN);

        HFONT f_title = make_font(50, FW_BOLD, 0);
        HFONT f_key   = make_font(22, FW_NORMAL, 0);
        HFONT f_mono  = make_font(15, FW_NORMAL, 0);
        HFONT f_timer = make_font(30, FW_BOLD, 0);
        HFONT f_btn   = make_font(20, FW_BOLD, 0);
        HFONT f_demo  = make_font(14, FW_NORMAL, 1);
        HFONT f_glitch= make_font(18, FW_BOLD, 1);

        HWND h1 = CreateWindowW(L"STATIC",
            L"\u26A0  ALL YOUR FILES ARE ENCRYPTED  \u26A0",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            0, 20, W, 70, hwnd, (HMENU)IDC_HEADER, NULL, NULL);
        SendMessageW(h1, WM_SETFONT, (WPARAM)f_title, TRUE);

        HWND h_glitch = CreateWindowW(L"STATIC", L"ENCRYPTING...",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            0, 90, W, 30, hwnd, (HMENU)IDC_GLITCH, NULL, NULL);
        SendMessageW(h_glitch, WM_SETFONT, (WPARAM)f_glitch, TRUE);
        g_hwnd_glitch = h_glitch;

        wchar_t kt[128];
        _snwprintf(kt, 128, L"Personal key:  %ls", FAKE_KEY);
        HWND h2 = CreateWindowW(L"STATIC", kt,
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            0, 130, W, 35, hwnd, (HMENU)IDC_KEY, NULL, NULL);
        SendMessageW(h2, WM_SETFONT, (WPARAM)f_key, TRUE);

        HWND h3 = CreateWindowW(L"STATIC",
            L"Send 0.05 BTC to the address below. You have 72 hours.",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            0, 185, W, 30, hwnd, (HMENU)IDC_BTC_LABEL, NULL, NULL);
        SendMessageW(h3, WM_SETFONT, (WPARAM)f_key, TRUE);

        HWND h4 = CreateWindowW(L"STATIC", DECOY_BTC,
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            0, 220, W, 35, hwnd, (HMENU)IDC_BTC_ADDR, NULL, NULL);
        SendMessageW(h4, WM_SETFONT, (WPARAM)f_key, TRUE);

        HWND h_btn = CreateWindowW(L"BUTTON", L"I'VE PAID \u2014 DECRYPT (FAKE)",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            W/2 - 220, 275, 440, 55,
            hwnd, (HMENU)IDC_PAY_BTN, NULL, NULL);
        SendMessageW(h_btn, WM_SETFONT, (WPARAM)f_btn, TRUE);

        g_hwnd_log = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL |
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            60, 350, W - 120, H - 620,
            hwnd, (HMENU)IDC_LOG, NULL, NULL);
        SendMessageW(g_hwnd_log, WM_SETFONT, (WPARAM)f_mono, TRUE);
        SendMessageW(g_hwnd_log, EM_SETBKGNDCOLOR, 0, RGB(0, 0, 0));
        SendMessageW(g_hwnd_log, EM_SETLIMITTEXT, 0x7FFFFFFE, 0);

        HWND h_kc = CreateWindowW(L"STATIC", L"Processes terminated (sim): 0",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            0, H - 250, W, 28, hwnd, (HMENU)IDC_FAKEKILL, NULL, NULL);
        SendMessageW(h_kc, WM_SETFONT, (WPARAM)f_key, TRUE);
        g_hwnd_fakekill = h_kc;

        g_hwnd_timer = CreateWindowW(L"STATIC", L"TIME LEFT: --",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            0, H - 215, W, 38, hwnd, (HMENU)IDC_TIMER, NULL, NULL);
        SendMessageW(g_hwnd_timer, WM_SETFONT, (WPARAM)f_timer, TRUE);

        HWND h_demo = CreateWindowW(L"STATIC",
            L"SAFE DEMO \u2014 no real data modified \u2014 Esc / EXIT to quit",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            0, H - 170, W, 26, hwnd, (HMENU)IDC_DEMO, NULL, NULL);
        SendMessageW(h_demo, WM_SETFONT, (WPARAM)f_demo, TRUE);

        HWND h_exit = CreateWindowW(L"BUTTON", L"EXIT SIMULATOR",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            W/2 - 150, H - 135, 300, 48,
            hwnd, (HMENU)IDC_EXIT_BTN, NULL, NULL);
        SendMessageW(h_exit, WM_SETFONT, (WPARAM)f_btn, TRUE);

        g_start_time = time(NULL);
        SetTimer(hwnd, 1, 1000, NULL);
        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(wp) == IDC_PAY_BTN) {
            MessageBoxW(hwnd,
                L"Thank you (DEMO).\n\n"
                L"No real decryption occurred. Sandbox files can be deleted.\n"
                L"The simulator will now close and restore your wallpaper.",
                L"FAKE DECRYPT ACCEPTED", MB_OK | MB_ICONINFORMATION);
            g_running = 0;
            DestroyWindow(hwnd);
        }
        if (LOWORD(wp) == IDC_EXIT_BTN) {
            g_running = 0;
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_TIMER:
        update_timer(g_hwnd_timer);
        return 0;

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wp;
        SetBkColor(hdc, RGB(3, 3, 3));
        int id = GetDlgCtrlID((HWND)lp);
        switch (id) {
        case IDC_HEADER:     SetTextColor(hdc, RGB(255, 20, 20)); break;
        case IDC_KEY:        SetTextColor(hdc, RGB(255, 170, 0)); break;
        case IDC_BTC_LABEL:  SetTextColor(hdc, RGB(255, 100, 100)); break;
        case IDC_BTC_ADDR:   SetTextColor(hdc, RGB(51, 255, 51)); break;
        case IDC_FAKEKILL:   SetTextColor(hdc, RGB(255, 80, 80)); break;
        case IDC_TIMER:      SetTextColor(hdc, RGB(255, 20, 20)); break;
        case IDC_DEMO:       SetTextColor(hdc, RGB(0, 170, 255)); break;
        case IDC_GLITCH:     SetTextColor(hdc, RGB(255, 0, 150)); break;
        default:             SetTextColor(hdc, RGB(200, 200, 200));
        }
        return (LRESULT)bg;
    }

    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wp;
        SetBkColor(hdc, RGB(0, 0, 0));
        SetTextColor(hdc, RGB(51, 255, 51));
        return (LRESULT)GetStockObject(BLACK_BRUSH);
    }

    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect((HDC)wp, &rc, bg);
        return 1;
    }

    case WM_CLOSE: {
        /* Không thoát ngay — nhưng KHÔNG chặn cứng. */
        int r = MessageBoxW(hwnd,
            L"Are you sure you want to exit the simulator?\n\n"
            L"(This is a SAFE DEMO \u2014 no real files are affected.)",
            L"Exit Simulation?",
            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
        if (r == IDYES) {
            g_running = 0;
            DestroyWindow(hwnd);
        }
        return 0;
    }

    case WM_DESTROY:
        g_running = 0;
        KillTimer(hwnd, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ============================================================
 * MAIN
 * ============================================================ */

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev,
                    PWSTR cmdline, int show) {
    (void)hPrev; (void)cmdline; (void)show;

    /* Đăng ký atexit NGAY ĐẦU — trước khi đụng wallpaper */
    register_wallpaper_safety();

    get_sandbox_path();
    srand((unsigned)time(NULL));

    save_old_wallpaper();
    build_sandbox();

    int w = GetSystemMetrics(SM_CXSCREEN);
    int h = GetSystemMetrics(SM_CYSCREEN);
    HBITMAP bmp = create_wallpaper_bitmap(w, h);
    save_bitmap_to_bmp(bmp, g_cache);
    DeleteObject(bmp);
    set_wallpaper(g_cache);

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"RansomSimSafe2";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(3, 3, 3));
    RegisterClassW(&wc);

    g_hwnd_main = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"RansomSimSafe2", L"RESTORE FILES [SAFE DEMO]",
        WS_POPUP | WS_VISIBLE,
        0, 0, w, h,
        NULL, NULL, hInst, NULL);

    SetWindowPos(g_hwnd_main, HWND_TOPMOST, 0, 0, w, h, SWP_SHOWWINDOW);

    drop_note();

    CreateThread(NULL, 0, encrypt_thread,      NULL, 0, NULL);
    CreateThread(NULL, 0, fake_killer_thread,  NULL, 0, NULL);
    CreateThread(NULL, 0, glitch_thread,       NULL, 0, NULL);

    RegisterHotKey(g_hwnd_main, 1, 0, VK_ESCAPE);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (msg.message == WM_HOTKEY && msg.wParam == 1) {
            /* Esc — cũng qua dialog xác nhận */
            SendMessageW(g_hwnd_main, WM_CLOSE, 0, 0);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    /* Dự phòng — atexit cũng sẽ chạy */
    restore_wallpaper();
    return 0;
}
