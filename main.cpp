#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <shlwapi.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <string>
#include <memory>
#include <mutex>
#include <vector>
#include <algorithm>
#include "resource.h"
#include "ProxyChecker.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#define WM_PROXY_UPDATE (WM_USER + 100)
#define WM_CHECK_COMPLETE (WM_USER + 101)

// Colors
#define COLOR_BG RGB(245, 247, 250)
#define COLOR_SUCCESS RGB(39, 174, 96)
#define COLOR_FAIL RGB(231, 76, 60)
#define COLOR_BTN_PRIMARY RGB(52, 152, 219)
#define COLOR_BTN_DANGER RGB(231, 76, 60)
#define COLOR_BTN_SUCCESS RGB(39, 174, 96)

// Global variables
HWND g_hWnd = nullptr;
HWND g_hListView = nullptr;
HWND g_hProgress = nullptr;
HWND g_hProgressFrame = nullptr;
HWND g_hStatus = nullptr;
HWND g_hStats = nullptr;
HWND g_hLog = nullptr;
HWND g_hTimeout = nullptr;
HWND g_hThreads = nullptr;
HWND g_hBtnStart = nullptr;
HWND g_hBtnStop = nullptr;
HWND g_hBtnOpen = nullptr;
HWND g_hBtnPaste = nullptr;
HWND g_hBtnClear = nullptr;
HWND g_hBtnCopy = nullptr;
HWND g_hBtnSave = nullptr;
HWND g_hBtnLookup = nullptr;
HWND g_hBtnDelFailed = nullptr;
HWND g_hBtnRecheck = nullptr;
HWND g_hChkAuto = nullptr;
HWND g_hEditInterval = nullptr;

// Auto check
#define AUTO_CHECK_TIMER_ID 1002
bool g_autoCheckEnabled = false;
int g_autoCheckInterval = 60;  // seconds
int g_autoCheckCountdown = 0;

// Sorting
int g_sortColumn = -1;
bool g_sortAscending = true;

std::unique_ptr<ProxyChecker> g_checker;
HFONT g_hFont = nullptr;
HFONT g_hFontBold = nullptr;
HBRUSH g_hBrushBg = nullptr;

// Update batching
#define UPDATE_TIMER_ID 1001
#define UPDATE_INTERVAL 150
std::mutex g_updateMutex;
bool g_needsUpdate = false;
int g_lastUpdatedCount = 0;

// Function declarations
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
void CreateControls(HWND hWnd);
void ResizeControls(HWND hWnd);
void UpdateStats();
void AppendLog(const std::wstring& text);
void OnOpenFile();
void OnPaste();
void OnStartCheck();
void OnStopCheck();
void OnClear();
void OnCopyWorking();
void OnSaveWorking();
void SortResultsByLatency();
void CopyToClipboard(const std::wstring& text);
void ShowContextMenu(HWND hWnd, int x, int y);
void OnContextCopyProxy();
void OnContextCopyIp();
void OnContextCopyAll();
void OnContextRecheck();
void OnContextDelete();
void OnContextDeleteFailed();
void OnContextSelectAll();
void OnLookupProxy();
void OnContextLookup();
void OnDeleteFailed();
void OnRecheckFailed();
void OnColumnClick(int column);
std::wstring FormatUptime(const ProxyInfo& info);
void OnAutoCheckToggle();
void StartAutoCheckTimer();
void StopAutoCheckTimer();
void OnAutoCheckTick();
void OnOpenInChrome();

HWND CreateButton(HWND parent, LPCWSTR text, int x, int y, int w, int h, int id, HINSTANCE hInst) {
    return CreateWindowW(L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y, w, h, parent, (HMENU)(UINT_PTR)id, hInst, nullptr);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    INITCOMMONCONTROLSEX icex = {0};
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    WNDCLASSEXW wcex = {0};
    wcex.cbSize = sizeof(wcex);
    wcex.style = 0;  // No CS_HREDRAW | CS_VREDRAW to reduce flicker
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wcex.lpszClassName = L"ProxyCheckerClass";
    wcex.hIcon = LoadIcon(nullptr, IDI_APPLICATION);

    if (!RegisterClassExW(&wcex)) {
        MessageBoxW(nullptr, L"Failed to register window class", L"Error", MB_ICONERROR);
        return 1;
    }

    g_hWnd = CreateWindowExW(
        WS_EX_CONTROLPARENT,
        L"ProxyCheckerClass",
        L"Proxy Checker Pro v1.0",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1300, 850,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!g_hWnd) {
        MessageBoxW(nullptr, L"Failed to create window", L"Error", MB_ICONERROR);
        return 1;
    }

    // Dark title bar
    BOOL darkMode = TRUE;
    DwmSetWindowAttribute(g_hWnd, 20, &darkMode, sizeof(darkMode));

    g_hFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    g_hFontBold = CreateFontW(-14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    g_hBrushBg = CreateSolidBrush(COLOR_BG);

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (g_hFont) DeleteObject(g_hFont);
    if (g_hFontBold) DeleteObject(g_hFontBold);
    if (g_hBrushBg) DeleteObject(g_hBrushBg);

    return (int)msg.wParam;
}

void CreateControls(HWND hWnd) {
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtr(hWnd, GWLP_HINSTANCE);

    int y = 15, x = 15, btnH = 30, gap = 8;

    // Row 1 - File buttons
    g_hBtnOpen = CreateButton(hWnd, L"Open File", x, y, 90, btnH, IDC_BTN_OPEN, hInst);
    x += 90 + gap;
    g_hBtnPaste = CreateButton(hWnd, L"Paste", x, y, 70, btnH, IDC_BTN_PASTE, hInst);
    x += 70 + gap;
    g_hBtnSave = CreateButton(hWnd, L"Save Results", x, y, 100, btnH, IDC_BTN_SAVE, hInst);

    x += 120 + gap;
    CreateWindowW(L"STATIC", L"Timeout:", WS_CHILD | WS_VISIBLE, x, y + 6, 55, 20, hWnd, nullptr, hInst, nullptr);
    x += 55;
    g_hTimeout = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1",
        WS_CHILD | WS_VISIBLE | ES_CENTER | ES_NUMBER, x, y + 2, 40, 24, hWnd, (HMENU)IDC_EDIT_TIMEOUT, hInst, nullptr);
    x += 45;
    CreateWindowW(L"STATIC", L"s", WS_CHILD | WS_VISIBLE, x, y + 6, 15, 20, hWnd, nullptr, hInst, nullptr);

    x += 25;
    CreateWindowW(L"STATIC", L"Threads:", WS_CHILD | WS_VISIBLE, x, y + 6, 55, 20, hWnd, nullptr, hInst, nullptr);
    x += 55;
    g_hThreads = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"30",
        WS_CHILD | WS_VISIBLE | ES_CENTER | ES_NUMBER, x, y + 2, 45, 24, hWnd, (HMENU)IDC_EDIT_THREADS, hInst, nullptr);

    // Row 2 - Action buttons
    y += btnH + 10;
    x = 15;
    g_hBtnStart = CreateButton(hWnd, L"Start Check", x, y, 100, btnH, IDC_BTN_START, hInst);
    x += 100 + gap;
    g_hBtnStop = CreateButton(hWnd, L"Stop", x, y, 60, btnH, IDC_BTN_STOP, hInst);
    EnableWindow(g_hBtnStop, FALSE);
    x += 60 + gap;
    g_hBtnClear = CreateButton(hWnd, L"Clear", x, y, 60, btnH, IDC_BTN_CLEAR, hInst);
    x += 60 + gap;
    g_hBtnCopy = CreateButton(hWnd, L"Copy Working", x, y, 110, btnH, IDC_BTN_COPY, hInst);
    x += 110 + gap;
    g_hBtnLookup = CreateButton(hWnd, L"Lookup IP", x, y, 90, btnH, IDC_BTN_LOOKUP, hInst);
    x += 90 + gap;
    g_hBtnDelFailed = CreateButton(hWnd, L"Del Failed", x, y, 85, btnH, IDC_BTN_DEL_FAILED, hInst);
    x += 85 + gap;
    g_hBtnRecheck = CreateButton(hWnd, L"Recheck Failed", x, y, 110, btnH, IDC_BTN_RECHECK, hInst);

    // Auto check controls
    x += 110 + gap + 10;
    g_hChkAuto = CreateWindowW(L"BUTTON", L"Auto",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        x, y + 5, 50, 20, hWnd, (HMENU)IDC_CHK_AUTO, hInst, nullptr);
    x += 50;
    g_hEditInterval = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"60",
        WS_CHILD | WS_VISIBLE | ES_CENTER | ES_NUMBER, x, y + 2, 40, 24, hWnd, (HMENU)IDC_EDIT_INTERVAL, hInst, nullptr);
    x += 42;
    CreateWindowW(L"STATIC", L"s", WS_CHILD | WS_VISIBLE, x, y + 6, 15, 20, hWnd, nullptr, hInst, nullptr);

    // Row 3 - Progress bar area
    y += btnH + 15;
    int progressY = y;

    g_hProgress = CreateWindowExW(WS_EX_CLIENTEDGE, PROGRESS_CLASSW, nullptr,
        WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
        15, y, 500, 22, hWnd, (HMENU)IDC_PROGRESS, hInst, nullptr);
    SendMessage(g_hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    SendMessage(g_hProgress, PBM_SETBARCOLOR, 0, RGB(46, 204, 113));

    g_hStatus = CreateWindowW(L"STATIC", L"Ready - Load proxies to begin",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 530, y + 2, 700, 20, hWnd, (HMENU)IDC_STATUS, hInst, nullptr);

    y += 32;

    // ListView - Multi-select mode
    g_hListView = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        WC_LISTVIEWW,
        nullptr,
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
        15, y, 1250, 500,
        hWnd, (HMENU)IDC_LISTVIEW, hInst, nullptr
    );

    // Double buffering is key for flicker-free
    ListView_SetExtendedListViewStyle(g_hListView,
        LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);

    ListView_SetBkColor(g_hListView, RGB(255, 255, 255));
    ListView_SetTextBkColor(g_hListView, RGB(255, 255, 255));

    // Columns
    LVCOLUMNW lvc = {0};
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM | LVCF_FMT;

    const wchar_t* columns[] = {L"#", L"Proxy", L"Status", L"Fails", L"Uptime", L"Anonymity", L"Time", L"External IP", L"Country", L"City", L"ISP"};
    int widths[] = {45, 260, 80, 45, 80, 85, 65, 120, 90, 90, 180};

    for (int i = 0; i < 11; i++) {
        lvc.iSubItem = i;
        lvc.pszText = const_cast<LPWSTR>(columns[i]);
        lvc.cx = widths[i];
        lvc.fmt = (i == 0 || i == 3 || i == 4) ? LVCFMT_CENTER : LVCFMT_LEFT;
        ListView_InsertColumn(g_hListView, i, &lvc);
    }

    // Stats
    g_hStats = CreateWindowW(L"STATIC", L"Total: 0  |  Success: 0  |  Failed: 0  |  Anonymous: 0",
        WS_CHILD | WS_VISIBLE, 15, 0, 900, 22, hWnd, (HMENU)IDC_STATS, hInst, nullptr);

    // Log
    g_hLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        15, 0, 1250, 85, hWnd, (HMENU)IDC_LOG, hInst, nullptr);

    // Apply fonts
    EnumChildWindows(hWnd, [](HWND hwndChild, LPARAM lParam) -> BOOL {
        SendMessage(hwndChild, WM_SETFONT, lParam, TRUE);
        return TRUE;
    }, (LPARAM)g_hFont);

    SendMessage(g_hStats, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

    g_checker = std::make_unique<ProxyChecker>(hWnd);

    AppendLog(L"Proxy Checker Pro v1.0 ready");
}

void ResizeControls(HWND hWnd) {
    RECT rc;
    GetClientRect(hWnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    int margin = 15;
    int progressY = 100;
    int listViewTop = 135;
    int logHeight = 90;
    int statsHeight = 25;
    int listViewHeight = height - listViewTop - logHeight - statsHeight - margin * 2;

    if (listViewHeight < 150) listViewHeight = 150;

    // Progress bar and status
    if (g_hProgress) {
        SetWindowPos(g_hProgress, nullptr, margin, progressY, 500, 22, SWP_NOZORDER);
    }
    if (g_hStatus) {
        SetWindowPos(g_hStatus, nullptr, 530, progressY + 2, width - 550, 20, SWP_NOZORDER);
    }

    // ListView
    if (g_hListView) {
        SetWindowPos(g_hListView, nullptr, margin, listViewTop, width - margin * 2, listViewHeight, SWP_NOZORDER);
    }

    // Stats below ListView
    if (g_hStats) {
        SetWindowPos(g_hStats, nullptr, margin, listViewTop + listViewHeight + 5, width - margin * 2, statsHeight, SWP_NOZORDER);
    }

    // Log at bottom
    if (g_hLog) {
        SetWindowPos(g_hLog, nullptr, margin, listViewTop + listViewHeight + statsHeight + 10, width - margin * 2, logHeight, SWP_NOZORDER);
    }
}

// Format uptime as human readable string
std::wstring FormatUptime(const ProxyInfo& info) {
    if (!info.hasFirstSuccess) {
        return L"-";
    }

    auto now = std::chrono::system_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - info.firstSuccessTime).count();

    if (duration < 0) duration = 0;

    int days = (int)(duration / 86400);
    int hours = (int)((duration % 86400) / 3600);
    int minutes = (int)((duration % 3600) / 60);
    int seconds = (int)(duration % 60);

    wchar_t buf[64];
    if (days > 0) {
        swprintf_s(buf, L"%dd %dh %dm", days, hours, minutes);
    } else if (hours > 0) {
        swprintf_s(buf, L"%dh %dm %ds", hours, minutes, seconds);
    } else if (minutes > 0) {
        swprintf_s(buf, L"%dm %ds", minutes, seconds);
    } else {
        swprintf_s(buf, L"%ds", seconds);
    }
    return buf;
}

void UpdateListViewItem(int index, const ProxyInfo& info) {
    if (!g_hListView || index < 0) return;

    int count = ListView_GetItemCount(g_hListView);

    // Add new item if needed
    if (index >= count) {
        LVITEMW lvi = {0};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = index;
        wchar_t num[16];
        swprintf_s(num, L"%d", index + 1);
        lvi.pszText = num;
        ListView_InsertItem(g_hListView, &lvi);
    }

    // Update all columns
    wchar_t num[16];
    swprintf_s(num, L"%d", index + 1);
    ListView_SetItemText(g_hListView, index, 0, num);
    ListView_SetItemText(g_hListView, index, 1, const_cast<LPWSTR>(info.proxy.c_str()));
    ListView_SetItemText(g_hListView, index, 2, const_cast<LPWSTR>(info.status.c_str()));

    // Fail count column
    wchar_t failStr[16];
    swprintf_s(failStr, L"%d", info.failCount);
    ListView_SetItemText(g_hListView, index, 3, failStr);

    // Uptime column
    std::wstring uptimeStr = FormatUptime(info);
    ListView_SetItemText(g_hListView, index, 4, const_cast<LPWSTR>(uptimeStr.c_str()));

    ListView_SetItemText(g_hListView, index, 5, const_cast<LPWSTR>(info.anonymity.c_str()));
    ListView_SetItemText(g_hListView, index, 6, const_cast<LPWSTR>(info.responseTime.c_str()));
    ListView_SetItemText(g_hListView, index, 7, const_cast<LPWSTR>(info.externalIp.c_str()));
    ListView_SetItemText(g_hListView, index, 8, const_cast<LPWSTR>(info.country.c_str()));
    ListView_SetItemText(g_hListView, index, 9, const_cast<LPWSTR>(info.city.c_str()));
    ListView_SetItemText(g_hListView, index, 10, const_cast<LPWSTR>(info.isp.c_str()));
}

void ProcessBatchUpdate() {
    if (!g_checker) return;

    std::lock_guard<std::mutex> lock(g_updateMutex);

    const auto& results = g_checker->GetResults();
    int total = (int)results.size();

    if (total == 0 || g_lastUpdatedCount >= total) {
        if (g_needsUpdate) {
            UpdateStats();
            g_needsUpdate = false;
        }
        return;
    }

    // Batch update - disable redraw
    SendMessage(g_hListView, WM_SETREDRAW, FALSE, 0);

    for (int i = g_lastUpdatedCount; i < total; i++) {
        if (results[i].status != L"Waiting") {
            UpdateListViewItem(i, results[i]);
        }
    }

    g_lastUpdatedCount = total;

    // Re-enable redraw
    SendMessage(g_hListView, WM_SETREDRAW, TRUE, 0);

    UpdateStats();
    g_needsUpdate = false;
}

void UpdateStats() {
    if (!g_checker || !g_hStats) return;

    int total = g_checker->GetTotal();
    int completed = g_checker->GetCompleted();

    // Calculate from actual results to avoid counter mismatches
    const auto& results = g_checker->GetResults();
    int success = 0;
    int failed = 0;
    int anonymous = 0;
    int waiting = 0;

    for (const auto& r : results) {
        if (r.success) {
            success++;
            if (r.anonymity == L"Anonymous") {
                anonymous++;
            }
        } else if (r.status == L"Waiting") {
            waiting++;
        } else if (!r.status.empty() && r.status != L"Waiting") {
            failed++;
        }
    }

    wchar_t text[256];
    swprintf_s(text, L"Total: %d  |  Success: %d  |  Failed: %d  |  Anonymous: %d",
               total, success, failed, anonymous);
    SetWindowTextW(g_hStats, text);

    // Progress: based on non-waiting items
    int checked = success + failed;
    if (total > 0) {
        SendMessage(g_hProgress, PBM_SETPOS, (checked * 100) / total, 0);
    }

    wchar_t status[256];
    if (g_checker->IsRunning()) {
        swprintf_s(status, L"Checking... %d / %d  (%.1f%%)", checked, total,
            total > 0 ? (checked * 100.0 / total) : 0.0);
    } else if (g_autoCheckEnabled && g_autoCheckCountdown > 0) {
        swprintf_s(status, L"Complete - %d successful | Next auto check in %ds", success, g_autoCheckCountdown);
    } else if (checked > 0) {
        swprintf_s(status, L"Complete - %d successful out of %d", success, total);
    } else {
        swprintf_s(status, L"Ready - %d proxies loaded", total);
    }
    SetWindowTextW(g_hStatus, status);
}

void AppendLog(const std::wstring& text) {
    if (!g_hLog) return;

    SYSTEMTIME st;
    GetLocalTime(&st);

    wchar_t logLine[1024];
    swprintf_s(logLine, L"[%02d:%02d:%02d] %s\r\n", st.wHour, st.wMinute, st.wSecond, text.c_str());

    int len = GetWindowTextLengthW(g_hLog);
    SendMessage(g_hLog, EM_SETSEL, len, len);
    SendMessage(g_hLog, EM_REPLACESEL, FALSE, (LPARAM)logLine);
}

void OnOpenFile() {
    OPENFILENAMEW ofn = {0};
    wchar_t filename[MAX_PATH] = {0};

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hWnd;
    ofn.lpstrFilter = L"Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (GetOpenFileNameW(&ofn)) {
        ListView_DeleteAllItems(g_hListView);
        g_lastUpdatedCount = 0;
        g_checker->LoadFromFile(filename);

        // Initial load - add all items
        SendMessage(g_hListView, WM_SETREDRAW, FALSE, 0);
        for (const auto& info : g_checker->GetResults()) {
            UpdateListViewItem(info.index, info);
        }
        SendMessage(g_hListView, WM_SETREDRAW, TRUE, 0);

        g_lastUpdatedCount = g_checker->GetTotal();
        AppendLog(L"Loaded " + std::to_wstring(g_checker->GetTotal()) + L" proxies from file");
        UpdateStats();
    }
}

void OnPaste() {
    ListView_DeleteAllItems(g_hListView);
    g_lastUpdatedCount = 0;
    g_checker->LoadFromClipboard();

    if (g_checker->GetTotal() == 0) {
        MessageBoxW(g_hWnd, L"No proxy format found in clipboard.\n\nSupported:\n- IP:PORT\n- http://user:pass@IP:PORT", L"Warning", MB_ICONWARNING);
        return;
    }

    SendMessage(g_hListView, WM_SETREDRAW, FALSE, 0);
    for (const auto& info : g_checker->GetResults()) {
        UpdateListViewItem(info.index, info);
    }
    SendMessage(g_hListView, WM_SETREDRAW, TRUE, 0);

    g_lastUpdatedCount = g_checker->GetTotal();
    AppendLog(L"Loaded " + std::to_wstring(g_checker->GetTotal()) + L" proxies from clipboard");
    UpdateStats();
}

void OnStartCheck() {
    if (!g_checker || g_checker->GetTotal() == 0) {
        MessageBoxW(g_hWnd, L"No proxies loaded.", L"Warning", MB_ICONWARNING);
        return;
    }

    wchar_t timeoutStr[16], threadsStr[16];
    GetWindowTextW(g_hTimeout, timeoutStr, 16);
    GetWindowTextW(g_hThreads, threadsStr, 16);

    int timeout = _wtoi(timeoutStr);
    int threads = _wtoi(threadsStr);
    if (timeout <= 0) timeout = 3;
    if (threads <= 0) threads = 30;

    EnableWindow(g_hBtnStart, FALSE);
    EnableWindow(g_hBtnStop, TRUE);

    g_lastUpdatedCount = 0;
    AppendLog(L"Starting check (" + std::to_wstring(threads) + L" threads, " + std::to_wstring(timeout) + L"s timeout)");

    g_checker->StartCheck(timeout, threads);
}

void OnStopCheck() {
    if (g_checker) g_checker->StopCheck();
    EnableWindow(g_hBtnStart, TRUE);
    EnableWindow(g_hBtnStop, FALSE);
    AppendLog(L"Stopped by user");
    ProcessBatchUpdate();
}

void OnClear() {
    if (g_checker) g_checker->Clear();
    ListView_DeleteAllItems(g_hListView);
    g_lastUpdatedCount = 0;
    SendMessage(g_hProgress, PBM_SETPOS, 0, 0);
    SetWindowTextW(g_hStatus, L"Ready - Load proxies to begin");
    SetWindowTextW(g_hStats, L"Total: 0  |  Success: 0  |  Failed: 0  |  Anonymous: 0");
    AppendLog(L"Cleared");
}

void OnCopyWorking() {
    if (!g_checker || g_checker->GetSuccess() == 0) {
        MessageBoxW(g_hWnd, L"No working proxies.", L"Info", MB_ICONINFORMATION);
        return;
    }
    g_checker->CopyWorking();
    AppendLog(L"Copied " + std::to_wstring(g_checker->GetSuccess()) + L" working proxies");
}

void OnSaveWorking() {
    if (!g_checker || g_checker->GetSuccess() == 0) {
        MessageBoxW(g_hWnd, L"No working proxies to save.", L"Warning", MB_ICONWARNING);
        return;
    }

    OPENFILENAMEW ofn = {0};
    wchar_t filename[MAX_PATH] = L"working_proxies.txt";

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hWnd;
    ofn.lpstrFilter = L"Text Files (*.txt)\0*.txt\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = L"txt";

    if (GetSaveFileNameW(&ofn)) {
        g_checker->SaveWorking(filename);
        AppendLog(L"Saved " + std::to_wstring(g_checker->GetSuccess()) + L" proxies");
    }
}

void SortResultsByLatency() {
    if (!g_checker || !g_hListView) return;

    const auto& results = g_checker->GetResults();
    if (results.empty()) return;

    // Create sorted indices: success first (by latency), then failures
    std::vector<int> sortedIndices;
    std::vector<std::pair<int, int>> successWithLatency;  // index, latency
    std::vector<int> failures;

    for (int i = 0; i < (int)results.size(); i++) {
        if (results[i].success) {
            int latency = 9999;
            try {
                std::wstring rt = results[i].responseTime;
                if (!rt.empty()) {
                    // Remove "ms" suffix if present
                    size_t pos = rt.find(L"ms");
                    if (pos != std::wstring::npos) {
                        rt = rt.substr(0, pos);
                    }
                    latency = std::stoi(rt);
                }
            } catch (...) {
                latency = 9999;
            }
            successWithLatency.push_back({i, latency});
        } else {
            failures.push_back(i);
        }
    }

    // Sort success by latency (ascending)
    std::sort(successWithLatency.begin(), successWithLatency.end(),
        [](const auto& a, const auto& b) { return a.second < b.second; });

    // Build sorted indices
    for (const auto& p : successWithLatency) {
        sortedIndices.push_back(p.first);
    }
    for (int idx : failures) {
        sortedIndices.push_back(idx);
    }

    // Update ListView with sorted order
    SendMessage(g_hListView, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_hListView);

    for (int newIndex = 0; newIndex < (int)sortedIndices.size(); newIndex++) {
        int origIndex = sortedIndices[newIndex];
        const auto& info = results[origIndex];

        LVITEMW lvi = {0};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = newIndex;
        wchar_t num[16];
        swprintf_s(num, L"%d", newIndex + 1);
        lvi.pszText = num;
        ListView_InsertItem(g_hListView, &lvi);

        ListView_SetItemText(g_hListView, newIndex, 1, const_cast<LPWSTR>(info.proxy.c_str()));
        ListView_SetItemText(g_hListView, newIndex, 2, const_cast<LPWSTR>(info.status.c_str()));

        wchar_t failStr[16];
        swprintf_s(failStr, L"%d", info.failCount);
        ListView_SetItemText(g_hListView, newIndex, 3, failStr);

        // Uptime column
        std::wstring uptimeStr = FormatUptime(info);
        ListView_SetItemText(g_hListView, newIndex, 4, const_cast<LPWSTR>(uptimeStr.c_str()));

        ListView_SetItemText(g_hListView, newIndex, 5, const_cast<LPWSTR>(info.anonymity.c_str()));
        ListView_SetItemText(g_hListView, newIndex, 6, const_cast<LPWSTR>(info.responseTime.c_str()));
        ListView_SetItemText(g_hListView, newIndex, 7, const_cast<LPWSTR>(info.externalIp.c_str()));
        ListView_SetItemText(g_hListView, newIndex, 8, const_cast<LPWSTR>(info.country.c_str()));
        ListView_SetItemText(g_hListView, newIndex, 9, const_cast<LPWSTR>(info.city.c_str()));
        ListView_SetItemText(g_hListView, newIndex, 10, const_cast<LPWSTR>(info.isp.c_str()));
    }

    SendMessage(g_hListView, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_hListView, nullptr, FALSE);

    AppendLog(L"Sorted: " + std::to_wstring(successWithLatency.size()) + L" success (by latency), " +
              std::to_wstring(failures.size()) + L" failed at bottom");
}

void CopyToClipboard(const std::wstring& text) {
    if (OpenClipboard(g_hWnd)) {
        EmptyClipboard();
        size_t size = (text.length() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
        if (hMem) {
            wchar_t* pMem = (wchar_t*)GlobalLock(hMem);
            if (pMem) {
                wcscpy_s(pMem, text.length() + 1, text.c_str());
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem);
            }
        }
        CloseClipboard();
    }
}

int GetSelectedCount() {
    return ListView_GetSelectedCount(g_hListView);
}

void ShowContextMenu(HWND hWnd, int x, int y) {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    int selectedCount = GetSelectedCount();
    bool hasSelection = selectedCount > 0;

    wchar_t copyProxyText[64];
    wchar_t deleteText[64];
    if (selectedCount > 1) {
        swprintf_s(copyProxyText, L"Copy %d Proxies", selectedCount);
        swprintf_s(deleteText, L"Delete %d Selected", selectedCount);
    } else {
        wcscpy_s(copyProxyText, L"Copy Proxy");
        wcscpy_s(deleteText, L"Delete Selected");
    }

    AppendMenuW(hMenu, MF_STRING | (hasSelection ? 0 : MF_GRAYED), IDM_CTX_COPY_PROXY, copyProxyText);
    AppendMenuW(hMenu, MF_STRING | (hasSelection ? 0 : MF_GRAYED), IDM_CTX_COPY_IP, L"Copy External IP");
    AppendMenuW(hMenu, MF_STRING | (hasSelection ? 0 : MF_GRAYED), IDM_CTX_COPY_ALL, L"Copy All Info");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING | (hasSelection ? 0 : MF_GRAYED), IDM_CTX_LOOKUP, L"Lookup IP Info");
    AppendMenuW(hMenu, MF_STRING | (hasSelection ? 0 : MF_GRAYED), IDM_CTX_OPEN_CHROME, L"Open in Chrome");
    AppendMenuW(hMenu, MF_STRING | (hasSelection ? 0 : MF_GRAYED), IDM_CTX_RECHECK, L"Recheck Selected");
    AppendMenuW(hMenu, MF_STRING | (hasSelection ? 0 : MF_GRAYED), IDM_CTX_DELETE, deleteText);
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_CTX_DELETE_FAILED, L"Delete All Failed");
    AppendMenuW(hMenu, MF_STRING, IDM_CTX_SELECT_ALL, L"Select All");

    TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, x, y, 0, hWnd, nullptr);
    DestroyMenu(hMenu);
}

void OnContextCopyProxy() {
    int selected = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
    if (selected < 0) return;

    std::wstring result;
    int count = 0;
    while (selected >= 0) {
        wchar_t buffer[512];
        ListView_GetItemText(g_hListView, selected, 1, buffer, 512);
        if (!result.empty()) result += L"\r\n";
        result += buffer;
        count++;
        selected = ListView_GetNextItem(g_hListView, selected, LVNI_SELECTED);
    }

    CopyToClipboard(result);
    AppendLog(L"Copied " + std::to_wstring(count) + L" proxies");
}

void OnContextCopyIp() {
    int selected = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
    if (selected < 0) return;

    std::wstring result;
    int count = 0;
    while (selected >= 0) {
        wchar_t buffer[256];
        ListView_GetItemText(g_hListView, selected, 7, buffer, 256);  // External IP is column 7 now
        if (wcslen(buffer) > 0) {
            if (!result.empty()) result += L"\r\n";
            result += buffer;
            count++;
        }
        selected = ListView_GetNextItem(g_hListView, selected, LVNI_SELECTED);
    }

    if (count > 0) {
        CopyToClipboard(result);
        AppendLog(L"Copied " + std::to_wstring(count) + L" IPs");
    }
}

void OnContextCopyAll() {
    int selected = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
    if (selected < 0) return;

    std::wstring result;
    int count = 0;
    while (selected >= 0) {
        wchar_t proxy[512], status[64], fails[16], uptime[64], anon[64], time[64], ip[256], country[128], city[128], isp[256];
        ListView_GetItemText(g_hListView, selected, 1, proxy, 512);
        ListView_GetItemText(g_hListView, selected, 2, status, 64);
        ListView_GetItemText(g_hListView, selected, 3, fails, 16);
        ListView_GetItemText(g_hListView, selected, 4, uptime, 64);
        ListView_GetItemText(g_hListView, selected, 5, anon, 64);
        ListView_GetItemText(g_hListView, selected, 6, time, 64);
        ListView_GetItemText(g_hListView, selected, 7, ip, 256);
        ListView_GetItemText(g_hListView, selected, 8, country, 128);
        ListView_GetItemText(g_hListView, selected, 9, city, 128);
        ListView_GetItemText(g_hListView, selected, 10, isp, 256);

        if (!result.empty()) result += L"\r\n";
        result += std::wstring(proxy) + L"\t" + status + L"\t" + fails + L"\t" + uptime + L"\t" + anon + L"\t" +
                  time + L"\t" + ip + L"\t" + country + L"\t" + city + L"\t" + isp;
        count++;
        selected = ListView_GetNextItem(g_hListView, selected, LVNI_SELECTED);
    }

    CopyToClipboard(result);
    AppendLog(L"Copied all info for " + std::to_wstring(count) + L" proxies");
}

void OnContextRecheck() {
    if (!g_checker || g_checker->IsRunning()) {
        MessageBoxW(g_hWnd, L"Please wait for current check to finish.", L"Info", MB_ICONINFORMATION);
        return;
    }

    // Collect selected indices
    std::vector<int> selectedIndices;
    int selected = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
    while (selected >= 0) {
        selectedIndices.push_back(selected);
        selected = ListView_GetNextItem(g_hListView, selected, LVNI_SELECTED);
    }

    if (selectedIndices.empty()) {
        MessageBoxW(g_hWnd, L"Select proxies to recheck.", L"Info", MB_ICONINFORMATION);
        return;
    }

    wchar_t timeoutStr[16], threadsStr[16];
    GetWindowTextW(g_hTimeout, timeoutStr, 16);
    GetWindowTextW(g_hThreads, threadsStr, 16);

    int timeout = _wtoi(timeoutStr);
    int threads = _wtoi(threadsStr);
    if (timeout <= 0) timeout = 3;
    if (threads <= 0) threads = 30;

    EnableWindow(g_hBtnStart, FALSE);
    EnableWindow(g_hBtnStop, TRUE);

    AppendLog(L"Rechecking " + std::to_wstring(selectedIndices.size()) + L" proxies");
    g_checker->RecheckIndices(selectedIndices, timeout, threads);
}

void OnContextDelete() {
    // Collect all selected indices first (in reverse order for safe deletion)
    std::vector<int> selectedIndices;
    int selected = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
    while (selected >= 0) {
        selectedIndices.push_back(selected);
        selected = ListView_GetNextItem(g_hListView, selected, LVNI_SELECTED);
    }

    if (selectedIndices.empty()) return;

    // Delete in reverse order to maintain correct indices
    SendMessage(g_hListView, WM_SETREDRAW, FALSE, 0);
    for (auto it = selectedIndices.rbegin(); it != selectedIndices.rend(); ++it) {
        ListView_DeleteItem(g_hListView, *it);
    }
    SendMessage(g_hListView, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_hListView, nullptr, FALSE);

    AppendLog(L"Deleted " + std::to_wstring(selectedIndices.size()) + L" items");
}

void OnContextDeleteFailed() {
    if (!g_checker) return;

    SendMessage(g_hListView, WM_SETREDRAW, FALSE, 0);

    int count = ListView_GetItemCount(g_hListView);
    int deleted = 0;

    for (int i = count - 1; i >= 0; i--) {
        wchar_t status[64];
        ListView_GetItemText(g_hListView, i, 2, status, 64);
        if (wcscmp(status, L"Success") != 0 && wcscmp(status, L"Waiting") != 0) {
            ListView_DeleteItem(g_hListView, i);
            deleted++;
        }
    }

    SendMessage(g_hListView, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_hListView, nullptr, FALSE);

    AppendLog(L"Deleted " + std::to_wstring(deleted) + L" failed proxies");
}

void OnContextSelectAll() {
    int count = ListView_GetItemCount(g_hListView);
    for (int i = 0; i < count; i++) {
        ListView_SetItemState(g_hListView, i, LVIS_SELECTED, LVIS_SELECTED);
    }
}

void OpenIpLookup(const std::wstring& input) {
    if (input.empty()) return;

    std::wstring ipOnly = input;

    // Handle http://user:pass@ip:port format
    if (ipOnly.find(L"http://") == 0 || ipOnly.find(L"https://") == 0) {
        // Remove http:// or https://
        size_t schemeEnd = ipOnly.find(L"://");
        if (schemeEnd != std::wstring::npos) {
            ipOnly = ipOnly.substr(schemeEnd + 3);
        }
        // Check for @ (user:pass@ip:port)
        size_t atPos = ipOnly.find(L'@');
        if (atPos != std::wstring::npos) {
            ipOnly = ipOnly.substr(atPos + 1);
        }
    }

    // Remove port (ip:port -> ip)
    size_t colonPos = ipOnly.find(L':');
    if (colonPos != std::wstring::npos) {
        ipOnly = ipOnly.substr(0, colonPos);
    }

    std::wstring url = L"https://ipinfo.io/" + ipOnly;
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void OnLookupProxy() {
    int selected = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
    if (selected < 0) {
        MessageBoxW(g_hWnd, L"Select a proxy first.", L"Info", MB_ICONINFORMATION);
        return;
    }

    // Get proxy address
    wchar_t proxy[512];
    ListView_GetItemText(g_hListView, selected, 1, proxy, 512);

    // Try to get external IP first, fall back to proxy address
    wchar_t ip[256];
    ListView_GetItemText(g_hListView, selected, 7, ip, 256);  // External IP is column 7 now

    if (wcslen(ip) > 0 && wcscmp(ip, L"-") != 0) {
        OpenIpLookup(ip);
        AppendLog(L"Opening IP lookup for: " + std::wstring(ip));
    } else {
        OpenIpLookup(proxy);
        AppendLog(L"Opening IP lookup for: " + std::wstring(proxy));
    }
}

void OnContextLookup() {
    OnLookupProxy();
}

void OnDeleteFailed() {
    OnContextDeleteFailed();
}

void OnRecheckFailed() {
    if (!g_checker || g_checker->IsRunning()) {
        MessageBoxW(g_hWnd, L"Please wait for current check to finish.", L"Info", MB_ICONINFORMATION);
        return;
    }

    // Count failed proxies
    const auto& results = g_checker->GetResults();
    int failedCount = 0;
    for (const auto& r : results) {
        if (!r.success && r.status != L"Waiting") {
            failedCount++;
        }
    }

    if (failedCount == 0) {
        MessageBoxW(g_hWnd, L"No failed proxies to recheck.", L"Info", MB_ICONINFORMATION);
        return;
    }

    wchar_t timeoutStr[16], threadsStr[16];
    GetWindowTextW(g_hTimeout, timeoutStr, 16);
    GetWindowTextW(g_hThreads, threadsStr, 16);

    int timeout = _wtoi(timeoutStr);
    int threads = _wtoi(threadsStr);
    if (timeout <= 0) timeout = 3;
    if (threads <= 0) threads = 30;

    EnableWindow(g_hBtnStart, FALSE);
    EnableWindow(g_hBtnStop, TRUE);

    AppendLog(L"Rechecking " + std::to_wstring(failedCount) + L" failed proxies");
    g_checker->RecheckFailed(timeout, threads);
}

// Compare function for ListView sorting
struct SortData {
    HWND hListView;
    int column;
    bool ascending;
};

int CALLBACK ListViewCompare(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort) {
    SortData* data = (SortData*)lParamSort;
    wchar_t buf1[512], buf2[512];

    ListView_GetItemText(data->hListView, (int)lParam1, data->column, buf1, 512);
    ListView_GetItemText(data->hListView, (int)lParam2, data->column, buf2, 512);

    int result = 0;

    // Column 0 (#), Column 3 (Fails), Column 6 (Time) - numeric sort
    if (data->column == 0 || data->column == 3 || data->column == 6) {
        int num1 = 0, num2 = 0;
        // Extract numeric part
        std::wstring s1(buf1), s2(buf2);
        // Remove "ms" suffix for time column
        size_t pos1 = s1.find(L"ms");
        size_t pos2 = s2.find(L"ms");
        if (pos1 != std::wstring::npos) s1 = s1.substr(0, pos1);
        if (pos2 != std::wstring::npos) s2 = s2.substr(0, pos2);
        try { num1 = std::stoi(s1); } catch (...) { num1 = 99999; }
        try { num2 = std::stoi(s2); } catch (...) { num2 = 99999; }
        result = num1 - num2;
    } else if (data->column == 4) {
        // Uptime column - parse time duration
        auto parseUptime = [](const std::wstring& s) -> int {
            if (s == L"-" || s.empty()) return -1;
            int total = 0;
            std::wstring num;
            for (wchar_t c : s) {
                if (c >= L'0' && c <= L'9') {
                    num += c;
                } else if (c == L'd' && !num.empty()) {
                    total += std::stoi(num) * 86400;
                    num.clear();
                } else if (c == L'h' && !num.empty()) {
                    total += std::stoi(num) * 3600;
                    num.clear();
                } else if (c == L'm' && !num.empty()) {
                    total += std::stoi(num) * 60;
                    num.clear();
                } else if (c == L's' && !num.empty()) {
                    total += std::stoi(num);
                    num.clear();
                }
            }
            return total;
        };
        int t1 = parseUptime(buf1);
        int t2 = parseUptime(buf2);
        result = t1 - t2;
    } else {
        // String sort
        result = wcscmp(buf1, buf2);
    }

    return data->ascending ? result : -result;
}

void OnColumnClick(int column) {
    if (g_sortColumn == column) {
        g_sortAscending = !g_sortAscending;
    } else {
        g_sortColumn = column;
        g_sortAscending = true;
    }

    // Store item indices in lParam for sorting
    int count = ListView_GetItemCount(g_hListView);
    for (int i = 0; i < count; i++) {
        LVITEMW lvi = {0};
        lvi.mask = LVIF_PARAM;
        lvi.iItem = i;
        lvi.lParam = i;
        ListView_SetItem(g_hListView, &lvi);
    }

    SortData sortData = { g_hListView, column, g_sortAscending };
    ListView_SortItemsEx(g_hListView, ListViewCompare, (LPARAM)&sortData);

    // Renumber after sort
    for (int i = 0; i < count; i++) {
        wchar_t num[16];
        swprintf_s(num, L"%d", i + 1);
        ListView_SetItemText(g_hListView, i, 0, num);
    }

    const wchar_t* columnNames[] = {L"#", L"Proxy", L"Status", L"Fails", L"Uptime", L"Anonymity", L"Time", L"External IP", L"Country", L"City", L"ISP"};
    AppendLog(std::wstring(L"Sorted by ") + columnNames[column] + (g_sortAscending ? L" (Asc)" : L" (Desc)"));
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        CreateControls(hWnd);
        SetTimer(hWnd, UPDATE_TIMER_ID, UPDATE_INTERVAL, nullptr);
        break;

    case WM_TIMER:
        if (wParam == UPDATE_TIMER_ID && g_needsUpdate) {
            ProcessBatchUpdate();
        } else if (wParam == AUTO_CHECK_TIMER_ID) {
            OnAutoCheckTick();
        }
        break;

    case WM_SIZE:
        ResizeControls(hWnd);
        break;

    case WM_NOTIFY:
        {
            LPNMHDR pnmh = (LPNMHDR)lParam;
            if (pnmh->hwndFrom == g_hListView) {
                switch (pnmh->code) {
                case NM_DBLCLK:
                    {
                        // Double click to copy proxy
                        LPNMITEMACTIVATE pnmia = (LPNMITEMACTIVATE)lParam;
                        if (pnmia->iItem >= 0) {
                            wchar_t buffer[512];
                            ListView_GetItemText(g_hListView, pnmia->iItem, 1, buffer, 512);
                            CopyToClipboard(buffer);
                            AppendLog(L"Copied: " + std::wstring(buffer));
                        }
                    }
                    break;

                case NM_RCLICK:
                    {
                        // Right click to show context menu
                        POINT pt;
                        GetCursorPos(&pt);
                        ShowContextMenu(hWnd, pt.x, pt.y);
                    }
                    break;

                case LVN_COLUMNCLICK:
                    {
                        LPNMLISTVIEW pnmlv = (LPNMLISTVIEW)lParam;
                        OnColumnClick(pnmlv->iSubItem);
                    }
                    break;

                case NM_CUSTOMDRAW:
                    {
                        LPNMLVCUSTOMDRAW pLVCD = (LPNMLVCUSTOMDRAW)lParam;

                        switch (pLVCD->nmcd.dwDrawStage) {
                        case CDDS_PREPAINT:
                            return CDRF_NOTIFYITEMDRAW;

                        case CDDS_ITEMPREPAINT:
                            {
                                int index = (int)pLVCD->nmcd.dwItemSpec;
                                // Get status from ListView directly for sorted view
                                wchar_t status[64];
                                wchar_t anon[64];
                                ListView_GetItemText(g_hListView, index, 2, status, 64);
                                ListView_GetItemText(g_hListView, index, 5, anon, 64);  // Anonymity is column 5 now

                                if (wcscmp(status, L"Success") == 0) {
                                    pLVCD->clrText = (wcscmp(anon, L"Anonymous") == 0) ? COLOR_SUCCESS : RGB(230, 126, 34);
                                } else if (wcscmp(status, L"Waiting") == 0) {
                                    pLVCD->clrText = RGB(128, 128, 128);
                                } else {
                                    pLVCD->clrText = COLOR_FAIL;
                                }

                                pLVCD->clrTextBk = (index % 2) ? RGB(248, 249, 250) : RGB(255, 255, 255);
                                return CDRF_NEWFONT;
                            }
                        }
                    }
                    break;
                }
            }
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_BTN_OPEN: OnOpenFile(); break;
        case IDC_BTN_PASTE: OnPaste(); break;
        case IDC_BTN_START: OnStartCheck(); break;
        case IDC_BTN_STOP: OnStopCheck(); break;
        case IDC_BTN_CLEAR: OnClear(); break;
        case IDC_BTN_COPY: OnCopyWorking(); break;
        case IDC_BTN_SAVE: OnSaveWorking(); break;
        // Context menu handlers
        case IDM_CTX_COPY_PROXY: OnContextCopyProxy(); break;
        case IDM_CTX_COPY_IP: OnContextCopyIp(); break;
        case IDM_CTX_COPY_ALL: OnContextCopyAll(); break;
        case IDM_CTX_RECHECK: OnContextRecheck(); break;
        case IDM_CTX_DELETE: OnContextDelete(); break;
        case IDM_CTX_DELETE_FAILED: OnContextDeleteFailed(); break;
        case IDM_CTX_SELECT_ALL: OnContextSelectAll(); break;
        case IDM_CTX_LOOKUP: OnContextLookup(); break;
        case IDC_BTN_LOOKUP: OnLookupProxy(); break;
        case IDC_BTN_DEL_FAILED: OnDeleteFailed(); break;
        case IDC_BTN_RECHECK: OnRecheckFailed(); break;
        case IDC_CHK_AUTO: OnAutoCheckToggle(); break;
        case IDM_CTX_OPEN_CHROME: OnOpenInChrome(); break;
        }
        break;

    case WM_PROXY_UPDATE:
        {
            ProxyInfo* pInfo = reinterpret_cast<ProxyInfo*>(lParam);
            if (pInfo) {
                // Direct update for this specific item
                UpdateListViewItem(pInfo->index, *pInfo);
                g_needsUpdate = true;
                delete pInfo;
            }
        }
        break;

    case WM_CHECK_COMPLETE:
        ProcessBatchUpdate();
        EnableWindow(g_hBtnStart, TRUE);
        EnableWindow(g_hBtnStop, FALSE);
        AppendLog(L"Complete: " + std::to_wstring(g_checker->GetSuccess()) + L" / " +
                  std::to_wstring(g_checker->GetTotal()) + L" successful");
        SortResultsByLatency();
        // Start auto check timer if enabled
        if (g_autoCheckEnabled && g_checker->GetSuccess() > 0) {
            StartAutoCheckTimer();
        }
        break;

    case WM_DESTROY:
        KillTimer(hWnd, UPDATE_TIMER_ID);
        KillTimer(hWnd, AUTO_CHECK_TIMER_ID);
        if (g_checker) g_checker->StopCheck();
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Auto check functions
void OnAutoCheckToggle() {
    g_autoCheckEnabled = (SendMessage(g_hChkAuto, BM_GETCHECK, 0, 0) == BST_CHECKED);

    if (g_autoCheckEnabled) {
        // Get interval from edit box
        wchar_t buf[16];
        GetWindowTextW(g_hEditInterval, buf, 16);
        g_autoCheckInterval = _wtoi(buf);
        if (g_autoCheckInterval < 10) g_autoCheckInterval = 10;  // Minimum 10 seconds
        if (g_autoCheckInterval > 3600) g_autoCheckInterval = 3600;  // Maximum 1 hour

        AppendLog(L"Auto check enabled: every " + std::to_wstring(g_autoCheckInterval) + L"s");

        // If not currently checking, start check immediately
        if (!g_checker->IsRunning() && g_checker->GetTotal() > 0) {
            OnStartCheck();
        }
    } else {
        StopAutoCheckTimer();
        AppendLog(L"Auto check disabled");
    }
}

void StartAutoCheckTimer() {
    g_autoCheckCountdown = g_autoCheckInterval;
    SetTimer(g_hWnd, AUTO_CHECK_TIMER_ID, 1000, nullptr);  // 1 second tick
    AppendLog(L"Next auto check in " + std::to_wstring(g_autoCheckCountdown) + L"s");
}

void StopAutoCheckTimer() {
    KillTimer(g_hWnd, AUTO_CHECK_TIMER_ID);
    g_autoCheckCountdown = 0;
}

void OnAutoCheckTick() {
    if (!g_autoCheckEnabled || g_checker->IsRunning()) {
        return;
    }

    g_autoCheckCountdown--;
    UpdateStats();  // Update countdown display

    if (g_autoCheckCountdown <= 0) {
        // Time to recheck successful proxies
        KillTimer(g_hWnd, AUTO_CHECK_TIMER_ID);

        // Collect successful proxy indices
        std::vector<int> successIndices;
        const auto& results = g_checker->GetResults();
        for (size_t i = 0; i < results.size(); i++) {
            if (results[i].success) {
                successIndices.push_back(static_cast<int>(i));
            }
        }

        if (successIndices.empty()) {
            AppendLog(L"Auto check: No successful proxies to recheck");
            return;
        }

        // Get settings
        wchar_t timeoutStr[16], threadsStr[16];
        GetWindowTextW(g_hTimeout, timeoutStr, 16);
        GetWindowTextW(g_hThreads, threadsStr, 16);

        int timeout = _wtoi(timeoutStr);
        int threads = _wtoi(threadsStr);
        if (timeout <= 0) timeout = 3;
        if (threads <= 0) threads = 30;

        EnableWindow(g_hBtnStart, FALSE);
        EnableWindow(g_hBtnStop, TRUE);

        AppendLog(L"Auto check: Rechecking " + std::to_wstring(successIndices.size()) + L" proxies");
        g_checker->RecheckIndices(successIndices, timeout, threads);
    }
}

// Helper function to write text file
bool WriteTextFile(const std::wstring& path, const std::string& content) {
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD written;
    WriteFile(hFile, content.c_str(), (DWORD)content.length(), &written, nullptr);
    CloseHandle(hFile);
    return true;
}

// Parse proxy URL into components
void ParseProxyComponents(const std::wstring& proxy, std::wstring& host, int& port, std::wstring& user, std::wstring& pass) {
    host.clear();
    port = 0;
    user.clear();
    pass.clear();

    std::wstring working = proxy;

    // Remove http:// or https://
    if (working.find(L"http://") == 0) {
        working = working.substr(7);
    } else if (working.find(L"https://") == 0) {
        working = working.substr(8);
    }

    // Check for @ (user:pass@host:port)
    size_t atPos = working.find(L'@');
    if (atPos != std::wstring::npos) {
        std::wstring auth = working.substr(0, atPos);
        working = working.substr(atPos + 1);

        // Parse user:pass
        size_t colonPos = auth.find(L':');
        if (colonPos != std::wstring::npos) {
            user = auth.substr(0, colonPos);
            pass = auth.substr(colonPos + 1);
        } else {
            user = auth;
        }
    }

    // Parse host:port
    size_t colonPos = working.find(L':');
    if (colonPos != std::wstring::npos) {
        host = working.substr(0, colonPos);
        port = _wtoi(working.substr(colonPos + 1).c_str());
    } else {
        host = working;
        port = 80;
    }
}

// Convert wstring to UTF-8 string
std::string WstrToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string str(size - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], size, nullptr, nullptr);
    return str;
}

void OnOpenInChrome() {
    int selected = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
    if (selected < 0) {
        MessageBoxW(g_hWnd, L"Select a proxy first.", L"Info", MB_ICONINFORMATION);
        return;
    }

    // Get proxy address
    wchar_t proxyBuf[512];
    ListView_GetItemText(g_hListView, selected, 1, proxyBuf, 512);
    std::wstring proxy(proxyBuf);

    if (proxy.empty()) {
        MessageBoxW(g_hWnd, L"Invalid proxy.", L"Error", MB_ICONERROR);
        return;
    }

    // Parse proxy components
    std::wstring host, user, pass;
    int port;
    ParseProxyComponents(proxy, host, port, user, pass);

    // Get the directory where the exe is located
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring exeDir = exePath;
    exeDir = exeDir.substr(0, exeDir.find_last_of(L"\\/"));

    // Build node command: node launch_chrome.js <host> <port> <user> <pass>
    std::wstring scriptPath = exeDir + L"\\launch_chrome.js";

    // Check if script exists
    if (GetFileAttributesW(scriptPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(g_hWnd, L"launch_chrome.js not found. Please place it next to the exe.", L"Error", MB_ICONERROR);
        return;
    }

    std::wstring cmdArgs = L"/c node \"" + scriptPath + L"\" " +
                           host + L" " + std::to_wstring(port) + L" " +
                           user + L" " + pass;

    HINSTANCE result = ShellExecuteW(nullptr, L"open", L"cmd.exe", cmdArgs.c_str(), exeDir.c_str(), SW_HIDE);

    if ((INT_PTR)result > 32) {
        AppendLog(L"Chrome launched with proxy: " + host + L":" + std::to_wstring(port));
        if (!user.empty()) {
            AppendLog(L"Auto-auth extension loaded for: " + user);
        }
    } else {
        MessageBoxW(g_hWnd, L"Failed to launch Chrome.", L"Error", MB_ICONERROR);
    }
}
