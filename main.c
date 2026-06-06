#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <ole2.h>
#include <commctrl.h>
#include <winhttp.h>
#include "resource.h"
#include "hash_utils.h"

#define REG_PATH L"Software\\HashCheck"

static HINSTANCE g_hInst = NULL;
static WCHAR g_verifyPath[MAX_PATH] = {0};
static WCHAR g_exePath[MAX_PATH] = {0};
static WCHAR g_exeDir[MAX_PATH] = {0};
static int g_btnPress = 0;
static int g_btnHover = 0;
static HWND g_hDropWnd = NULL;
static WCHAR g_dropZoneText[64] = L"拖放文件或 URL 到此处";
static BOOL g_logEnabled = FALSE;
static WCHAR g_cachePath[MAX_PATH] = {0};

// GDI resources (cached, recreated on DPI change)
static HFONT g_hTitleFont = NULL, g_hBodyFont = NULL;
static HBRUSH g_brGearPress = NULL, g_brGearHover = NULL;
static HBRUSH g_brClosePress = NULL, g_brCloseHover = NULL, g_brBodyBg = NULL;
static HPEN g_hBorderPen = NULL, g_hDashPen = NULL;

static void EnsureGdiResources(HWND hWnd)
{
    if (g_hTitleFont) return;
    int dpi = GetDpiForWindow(hWnd);
    g_hTitleFont = CreateFontW(-MulDiv(13, dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                DEFAULT_PITCH, L"Segoe UI");
    g_hBodyFont = CreateFontW(-MulDiv(16, dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                               DEFAULT_PITCH, L"Segoe UI");
    g_brGearPress = CreateSolidBrush(RGB(180, 180, 180));
    g_brGearHover = CreateSolidBrush(RGB(210, 210, 210));
    g_brClosePress = CreateSolidBrush(RGB(200, 30, 0));
    g_brCloseHover = CreateSolidBrush(RGB(232, 60, 30));
    g_brBodyBg = CreateSolidBrush(RGB(255, 255, 240));
    g_hBorderPen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_ACTIVEBORDER));
    g_hDashPen = CreatePen(PS_DASH, 1, RGB(0, 120, 215));
}

static void DestroyGdiResources(void)
{
    if (g_hTitleFont) { DeleteObject(g_hTitleFont); g_hTitleFont = NULL; }
    if (g_hBodyFont) { DeleteObject(g_hBodyFont); g_hBodyFont = NULL; }
    if (g_brGearPress) { DeleteObject(g_brGearPress); g_brGearPress = NULL; }
    if (g_brGearHover) { DeleteObject(g_brGearHover); g_brGearHover = NULL; }
    if (g_brClosePress) { DeleteObject(g_brClosePress); g_brClosePress = NULL; }
    if (g_brCloseHover) { DeleteObject(g_brCloseHover); g_brCloseHover = NULL; }
    if (g_brBodyBg) { DeleteObject(g_brBodyBg); g_brBodyBg = NULL; }
    if (g_hBorderPen) { DeleteObject(g_hBorderPen); g_hBorderPen = NULL; }
    if (g_hDashPen) { DeleteObject(g_hDashPen); g_hDashPen = NULL; }
}

static void NormalizeHash(const char* src, char* dst)
{
    int j = 0;
    for (int i = 0; src[i]; i++)
    {
        char c = src[i];
        if (isxdigit((unsigned char)c))
            dst[j++] = (char)toupper((unsigned char)c);
    }
    dst[j] = 0;
}

static void StripWhitespace(const WCHAR* src, WCHAR* dst)
{
    int j = 0;
    for (int i = 0; src[i]; i++)
    {
        if (src[i] != L' ' && src[i] != L'\t' && src[i] != L'\r' && src[i] != L'\n')
            dst[j++] = src[i];
    }
    dst[j] = 0;
}

static BOOL IsHexW(const WCHAR* s)
{
    for (int i = 0; s[i]; i++)
    {
        WCHAR c = s[i];
        if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') ||
              (c >= L'A' && c <= L'F')))
            return FALSE;
    }
    return TRUE;
}

INT_PTR CALLBACK VerifyDlgProc(HWND, UINT, WPARAM, LPARAM);

static BOOL EnsureDirectoryTree(const WCHAR* path)
{
    WCHAR tmp[MAX_PATH];
    wcscpy(tmp, path);
    for (WCHAR* p = tmp + 3; *p; p++)
    {
        if (*p == L'\\')
        {
            *p = 0;
            if (GetFileAttributesW(tmp) == INVALID_FILE_ATTRIBUTES)
                CreateDirectoryW(tmp, NULL);
            *p = L'\\';
        }
    }
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
        return CreateDirectoryW(path, NULL);
    return TRUE;
}

static void InitDefaultCachePath(void)
{
    WCHAR localAppData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData)))
        wsprintfW(g_cachePath, L"%s\\cache\\HashCheck", localAppData);
    else
        wcscpy(g_cachePath, L"cache");
}

static BOOL EnsureDir(WCHAR* path)
{
    DWORD attr = GetFileAttributesW(path);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
        return TRUE;

    WCHAR parent[MAX_PATH];
    wcscpy(parent, path);
    WCHAR* slash = wcsrchr(parent, L'\\');
    if (slash && slash != parent)
    {
        *slash = 0;
        if (!EnsureDir(parent))
            return FALSE;
    }
    return CreateDirectoryW(path, NULL) != 0;
}

static void CleanupTmp(void)
{
    if (g_cachePath[0] == 0) return;

    WCHAR findPath[MAX_PATH];
    wsprintfW(findPath, L"%s\\*", g_cachePath);
    WIN32_FIND_DATAW ffd;
    HANDLE hFind = FindFirstFileW(findPath, &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0)
            continue;
        WCHAR full[MAX_PATH];
        wsprintfW(full, L"%s\\%s", g_cachePath, ffd.cFileName);
        DeleteFileW(full);
    } while (FindNextFileW(hFind, &ffd));
    FindClose(hFind);
}

static void UpdateDropZoneText(const WCHAR* text)
{
    wcscpy(g_dropZoneText, text);
    if (g_hDropWnd)
    {
        InvalidateRect(g_hDropWnd, NULL, FALSE);
        UpdateWindow(g_hDropWnd);
    }
}

static void Log(const char* fmt, ...)
{
    if (!g_logEnabled) return;
    WCHAR logPath[MAX_PATH];
    wsprintfW(logPath, L"%s\\hashcheck.log", g_exeDir);
    FILE* f = _wfopen(logPath, L"a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fprintf(f, "\n");
    fclose(f);
}

static BOOL DownloadUrlToFile(const WCHAR* url, WCHAR* destPath)
{
    Log("DownloadUrlToFile start: url=%S", url);
    UpdateDropZoneText(L"下载中…");

    if (g_cachePath[0])
        EnsureDirectoryTree(g_cachePath);

    // Extract filename from URL
    WCHAR urlCopy[2048];
    wcscpy(urlCopy, url);
    WCHAR* lastSlash = wcsrchr(urlCopy, L'/');
    WCHAR* filename = lastSlash ? (lastSlash + 1) : urlCopy;
    WCHAR* query = wcschr(filename, L'?');
    if (query) *query = 0;

    Log("  lastSlash=%S filename=%S", lastSlash ? lastSlash : L"(null)", filename);

    if (filename[0] == 0 || wcschr(filename, L'.') == NULL)
        wsprintfW(destPath, L"%s\\download_%I64u", g_cachePath, GetTickCount64());
    else
        wsprintfW(destPath, L"%s\\%s", g_cachePath, filename);

    // Download via WinHTTP (more reliable TLS than URLDownloadToFileW)
    URL_COMPONENTSW uc = { sizeof(uc) };
    WCHAR hostName[256], urlPath[4096], extraInfo[256];
    uc.lpszHostName = hostName;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = urlPath;
    uc.dwUrlPathLength = 4096;
    uc.lpszExtraInfo = extraInfo;
    uc.dwExtraInfoLength = 256;

    BOOL ok = FALSE;
    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;
    HANDLE hFile = INVALID_HANDLE_VALUE;

    if (!WinHttpCrackUrl(url, 0, 0, &uc))
    {
        Log("  WinHttpCrackUrl failed");
        goto cleanup;
    }
    Log("  host=%S path=%S extra=%S", hostName, urlPath, extraInfo);

    hSession = WinHttpOpen(L"HashCheck/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) { Log("  WinHttpOpen failed"); goto cleanup; }

    hConnect = WinHttpConnect(hSession, hostName, uc.nPort, 0);
    if (!hConnect) { Log("  WinHttpConnect failed"); goto cleanup; }

    WCHAR fullPath[4096];
    wcscpy(fullPath, urlPath[0] ? urlPath : L"/");
    if (extraInfo[0])
        wcscat(fullPath, extraInfo);

    DWORD flags = uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    hRequest = WinHttpOpenRequest(hConnect, L"GET", fullPath, NULL, NULL, NULL, flags);
    if (!hRequest) { Log("  WinHttpOpenRequest failed"); goto cleanup; }

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
    { Log("  WinHttpSendRequest failed"); goto cleanup; }

    if (!WinHttpReceiveResponse(hRequest, NULL))
    { Log("  WinHttpReceiveResponse failed"); goto cleanup; }

    hFile = CreateFileW(destPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    { Log("  CreateFileW failed"); goto cleanup; }

    BYTE buf[65536];
    DWORD read;
    ok = TRUE;
    while (WinHttpReadData(hRequest, buf, sizeof(buf), &read) && read > 0)
    {
        DWORD written;
        if (!WriteFile(hFile, buf, read, &written, NULL))
        { ok = FALSE; break; }
    }

cleanup:
    if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);
    if (!ok) DeleteFileW(destPath);

    Log("  destPath=%S ok=%d", destPath, ok);

    UpdateDropZoneText(L"拖放文件或 URL 到此处");
    return ok;
}
static void ProcessDroppedFile(const WCHAR* path)
{
    wcscpy(g_verifyPath, path);
    DialogBoxW(g_hInst, MAKEINTRESOURCEW(IDD_VERIFY), NULL, VerifyDlgProc);
    g_verifyPath[0] = 0;
}

// IDropTarget implementation for URL drag from browsers
static HRESULT STDMETHODCALLTYPE DropTarget_QueryInterface(IDropTarget* this, REFIID riid, void** ppv)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDropTarget))
    {
        *ppv = this;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE DropTarget_AddRef(IDropTarget* this) { (void)this; return 1; }
static ULONG STDMETHODCALLTYPE DropTarget_Release(IDropTarget* this) { (void)this; return 1; }
static HRESULT STDMETHODCALLTYPE DropTarget_DragEnter(IDropTarget* this, IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect)
{
    (void)this; (void)pDataObj; (void)grfKeyState; (void)pt;
    *pdwEffect = DROPEFFECT_COPY;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE DropTarget_DragOver(IDropTarget* this, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect)
{
    (void)this; (void)grfKeyState; (void)pt;
    *pdwEffect = DROPEFFECT_COPY;
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE DropTarget_DragLeave(IDropTarget* this) { (void)this; return S_OK; }
static HRESULT STDMETHODCALLTYPE DropTarget_Drop(IDropTarget* this, IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect)
{
    (void)this; (void)grfKeyState; (void)pt;
    *pdwEffect = DROPEFFECT_NONE;
    Log("DropTarget_Drop enter");

    // Try CF_HDROP first (local file)
    FORMATETC fmt = { CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM med;
    if (SUCCEEDED(pDataObj->lpVtbl->GetData(pDataObj, &fmt, &med)))
    {
        Log("  CF_HDROP ok");
        HDROP hDrop = (HDROP)med.hGlobal;
        WCHAR filePath[MAX_PATH];
        if (DragQueryFileW(hDrop, 0, filePath, MAX_PATH) > 0)
        {
            Log("  filePath=%S", filePath);
            ProcessDroppedFile(filePath);
            *pdwEffect = DROPEFFECT_COPY;
        }
        ReleaseStgMedium(&med);
        Log("DropTarget_Drop return (CF_HDROP)");
        return S_OK;
    }
    Log("  CF_HDROP failed");

    // Try URL format
    static CLIPFORMAT cfURL = 0;
    if (cfURL == 0)
        cfURL = (CLIPFORMAT)RegisterClipboardFormatW(L"UniformResourceLocatorW");
    fmt.cfFormat = cfURL;
    fmt.tymed = TYMED_HGLOBAL;
    if (SUCCEEDED(pDataObj->lpVtbl->GetData(pDataObj, &fmt, &med)))
    {
        Log("  UniformResourceLocatorW ok");
        WCHAR* url = (WCHAR*)GlobalLock(med.hGlobal);
        if (url && url[0])
        {
            Log("  url=%S", url);
            WCHAR dest[MAX_PATH];
            if (DownloadUrlToFile(url, dest))
                ProcessDroppedFile(dest);
            GlobalUnlock(med.hGlobal);
        }
        ReleaseStgMedium(&med);
        *pdwEffect = DROPEFFECT_COPY;
        Log("DropTarget_Drop return (URL)");
        return S_OK;
    }
    Log("  UniformResourceLocatorW failed");

    // Try CF_UNICODETEXT as fallback
    fmt.cfFormat = CF_UNICODETEXT;
    fmt.tymed = TYMED_HGLOBAL;
    if (SUCCEEDED(pDataObj->lpVtbl->GetData(pDataObj, &fmt, &med)))
    {
        Log("  CF_UNICODETEXT ok");
        WCHAR* text = (WCHAR*)GlobalLock(med.hGlobal);
        if (text && text[0])
        {
            Log("  text=%S", text);
            if (wcsnicmp(text, L"http://", 7) == 0 || wcsnicmp(text, L"https://", 8) == 0 ||
                wcsnicmp(text, L"ftp://", 6) == 0)
            {
                WCHAR dest[MAX_PATH];
                if (DownloadUrlToFile(text, dest))
                    ProcessDroppedFile(dest);
            }
            else
                Log("  text is not a URL");
            GlobalUnlock(med.hGlobal);
        }
        ReleaseStgMedium(&med);
        *pdwEffect = DROPEFFECT_COPY;
        Log("DropTarget_Drop return (CF_UNICODETEXT)");
        return S_OK;
    }
    Log("  CF_UNICODETEXT failed");

    Log("DropTarget_Drop return (no format matched)");
    return S_OK;
}

static IDropTargetVtbl g_dropTargetVtbl = {
    DropTarget_QueryInterface,
    DropTarget_AddRef,
    DropTarget_Release,
    DropTarget_DragEnter,
    DropTarget_DragOver,
    DropTarget_DragLeave,
    DropTarget_Drop
};

static IDropTarget g_dropTarget = { &g_dropTargetVtbl };

static void RefreshResult(HWND hDlg)
{
    RedrawWindow(GetDlgItem(hDlg, IDC_RESULT), NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
}

static void CopyToClipboard(HWND hDlg, const WCHAR* text)
{
    if (text[0] && OpenClipboard(hDlg))
    {
        EmptyClipboard();
        DWORD size = (wcslen(text) + 1) * sizeof(WCHAR);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
        if (hMem)
        {
            memcpy(GlobalLock(hMem), text, size);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
        CloseClipboard();
    }
}

INT_PTR CALLBACK VerifyDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)lParam;
    static WCHAR md5[128], sha256[128];
    static BOOL successState = FALSE;
    static BOOL hasResult = FALSE;

    switch (msg)
    {
    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        if (hCtrl != GetDlgItem(hDlg, IDC_RESULT))
            return FALSE;
        SetBkMode(hdc, TRANSPARENT);
        if (hasResult)
            SetTextColor(hdc, successState ? RGB(0, 150, 0) : RGB(210, 0, 0));
        return (INT_PTR)GetSysColorBrush(COLOR_WINDOW);
    }

    case WM_INITDIALOG:
    {
        hasResult = FALSE;

        if (g_verifyPath[0] == 0)
        {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }

        md5[0] = 0; sha256[0] = 0;
        CalculateHashes(g_verifyPath, md5, sha256, 128);

        SetDlgItemTextW(hDlg, IDC_FILE_TEXT, g_verifyPath);

        SetDlgItemTextW(hDlg, IDC_MD5_TEXT, md5[0] ? md5 : L"(无法计算)");
        SetDlgItemTextW(hDlg, IDC_SHA256_TEXT, sha256[0] ? sha256 : L"(无法计算)");

        SetFocus(GetDlgItem(hDlg, IDC_HASH_EDIT));
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_VERIFY_BTN:
        {
            WCHAR expectedW[256] = {0};
            GetDlgItemTextW(hDlg, IDC_HASH_EDIT, expectedW, 256);

            char clean[256] = {0};
            WideCharToMultiByte(CP_ACP, 0, expectedW, -1, clean, 256, NULL, NULL);
            NormalizeHash(clean, clean);

            char m[128] = {0}, s[128] = {0};
            WideCharToMultiByte(CP_ACP, 0, md5, -1, m, 128, NULL, NULL);
            WideCharToMultiByte(CP_ACP, 0, sha256, -1, s, 128, NULL, NULL);

            if (strlen(clean) == 0)
            {
                hasResult = FALSE;
                SetDlgItemTextW(hDlg, IDC_RESULT, L"请输入要比较的哈希值");
                InvalidateRect(GetDlgItem(hDlg, IDC_RESULT), NULL, TRUE);
                return TRUE;
            }

            int len = (int)strlen(clean);
            BOOL matched = FALSE;
            const WCHAR* algo = L"";

            if (len == 32)
            {
                algo = L"MD5";
                matched = (_stricmp(clean, m) == 0);
            }
            else if (len == 64)
            {
                algo = L"SHA256";
                matched = (_stricmp(clean, s) == 0);
            }
            else
            {
                if (_stricmp(clean, m) == 0) { matched = TRUE; algo = L"MD5"; }
                if (!matched && _stricmp(clean, s) == 0) { matched = TRUE; algo = L"SHA256"; }
            }

            const WCHAR* matchStr = matched ? L"验证成功" : L"验证失败";
            WCHAR result[128];
            wsprintfW(result, algo[0] ? L"%s %s！" : L"%s%s！", algo, matchStr);

            successState = matched;
            hasResult = TRUE;
            SetDlgItemTextW(hDlg, IDC_RESULT, result);
            RefreshResult(hDlg);
            return TRUE;
        }

        case IDC_PASTE_BTN:
        {
            BOOL valid = FALSE;

            if (OpenClipboard(hDlg))
            {
                HANDLE hData = GetClipboardData(CF_UNICODETEXT);
                if (hData)
                {
                    WCHAR* text = (WCHAR*)GlobalLock(hData);
                    if (text)
                    {
                        WCHAR clean[256];
                        StripWhitespace(text, clean);
                        if (wcslen(clean) >= 32 && IsHexW(clean))
                        {
                            SetDlgItemTextW(hDlg, IDC_HASH_EDIT, clean);
                            hasResult = FALSE;
                            SetDlgItemTextW(hDlg, IDC_RESULT, L"");
                            RefreshResult(hDlg);
                            valid = TRUE;
                        }
                        GlobalUnlock(hData);
                    }
                }
                CloseClipboard();
            }

            if (!valid)
            {
                successState = FALSE;
                hasResult = TRUE;
                SetDlgItemTextW(hDlg, IDC_RESULT, L"剪切板里没有哈希数据");
                RefreshResult(hDlg);
            }
            return TRUE;
        }

        case IDC_COPY_MD5_BTN:
            CopyToClipboard(hDlg, md5);
            return TRUE;

        case IDC_COPY_SHA256_BTN:
            CopyToClipboard(hDlg, sha256);
            return TRUE;

        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static void ContextMenuAdd(void)
{
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
            L"Software\\Classes\\*\\shell\\HashCheck", 0, NULL,
            REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
    {
        WCHAR menuText[] = L"检查哈希(&H)";
        RegSetValueExW(hKey, NULL, 0, REG_SZ, (BYTE*)menuText, sizeof(menuText));
        RegSetValueExW(hKey, L"Icon", 0, REG_SZ, (BYTE*)g_exePath,
                     (wcslen(g_exePath) + 1) * sizeof(WCHAR));
        RegCloseKey(hKey);

        if (RegCreateKeyExW(HKEY_CURRENT_USER,
                L"Software\\Classes\\*\\shell\\HashCheck\\command", 0, NULL,
                REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
        {
            WCHAR cmdVal[MAX_PATH + 20];
            wsprintfW(cmdVal, L"\"%s\" \"%%1\"", g_exePath);
            RegSetValueExW(hKey, NULL, 0, REG_SZ, (BYTE*)cmdVal,
                         (wcslen(cmdVal) + 1) * sizeof(WCHAR));
            RegCloseKey(hKey);
        }
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    }
}

static void ContextMenuRemove(void)
{
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\*\\shell\\HashCheck");
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
}

static BOOL ContextMenuIsAdded(void)
{
    HKEY hKey;
    LONG ret = RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Classes\\*\\shell\\HashCheck", 0, KEY_READ, &hKey);
    if (ret == ERROR_SUCCESS)
    {
        RegCloseKey(hKey);
        return TRUE;
    }
    return FALSE;
}

INT_PTR CALLBACK SettingsDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)lParam;
    switch (msg)
    {
    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;

    case WM_INITDIALOG:
    {
        BOOL added = ContextMenuIsAdded();
        CheckDlgButton(hDlg, IDC_MENU_CHECKBOX, added ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_LOG_CHECKBOX, g_logEnabled ? BST_CHECKED : BST_UNCHECKED);
        SetDlgItemTextW(hDlg, IDC_CACHE_PATH, g_cachePath);

        RegisterDragDrop(hDlg, &g_dropTarget);
        return TRUE;
    }

    case WM_DESTROY:
        RevokeDragDrop(hDlg);
        return TRUE;

    case WM_NOTIFY:
    {
        LPNMHDR nm = (LPNMHDR)lParam;
        if (nm->idFrom == IDC_ABOUT_LINK && (nm->code == NM_CLICK || nm->code == NM_RETURN))
        {
            ShellExecuteW(NULL, L"open", L"https://github.com/kennykang717/HashChecker", NULL, NULL, SW_SHOW);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_MENU_CHECKBOX:
            if (HIWORD(wParam) == BN_CLICKED)
            {
                if (IsDlgButtonChecked(hDlg, IDC_MENU_CHECKBOX) == BST_CHECKED)
                    ContextMenuAdd();
                else
                    ContextMenuRemove();
            }
            return TRUE;

        case IDC_LOG_CHECKBOX:
            if (HIWORD(wParam) == BN_CLICKED)
            {
                g_logEnabled = (IsDlgButtonChecked(hDlg, IDC_LOG_CHECKBOX) == BST_CHECKED);
                HKEY hKey;
                if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, NULL,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
                {
                    DWORD val = g_logEnabled ? 1 : 0;
                    RegSetValueExW(hKey, L"LogEnabled", 0, REG_DWORD, (BYTE*)&val, sizeof(val));
                    RegCloseKey(hKey);
                }
            }
            return TRUE;

        case IDC_CACHE_BROWSE_BTN:
            if (HIWORD(wParam) == BN_CLICKED)
            {
                BROWSEINFOW bi = { 0 };
                bi.hwndOwner = hDlg;
                bi.lpszTitle = L"选择缓存目录";
                bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
                LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
                if (pidl)
                {
                    WCHAR path[MAX_PATH];
                    if (SHGetPathFromIDListW(pidl, path))
                    {
                        wsprintfW(g_cachePath, L"%s\\HashCheck", path);
                        SetDlgItemTextW(hDlg, IDC_CACHE_PATH, g_cachePath);
                        HKEY hKey;
                        if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, NULL,
                                REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS)
                        {
                            RegSetValueExW(hKey, L"CachePath", 0, REG_SZ, (BYTE*)g_cachePath,
                                         (wcslen(g_cachePath) + 1) * sizeof(WCHAR));
                            RegCloseKey(hKey);
                        }
                    }
                    CoTaskMemFree(pidl);
                }
            }
            return TRUE;

        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

#define TITLE_H 30
#define BTN_W 34

static void GetBtnRects(const RECT* rc, RECT* gear, RECT* close)
{
    if (gear) { gear->left = rc->right - 2 * BTN_W; gear->top = 0; gear->right = rc->right - BTN_W; gear->bottom = TITLE_H; }
    if (close) { close->left = rc->right - BTN_W; close->top = 0; close->right = rc->right; close->bottom = TITLE_H; }
}
static void InvalidateBtns(HWND hWnd)
{
    RECT rc, rcGear, rcClose, r;
    GetClientRect(hWnd, &rc);
    GetBtnRects(&rc, &rcGear, &rcClose);
    UnionRect(&r, &rcGear, &rcClose);
    InvalidateRect(hWnd, &r, FALSE);
}

static void PositionDropZone(HWND hWnd)
{
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int w = 400, h = 200;
    int x = work.right - w - 10;
    int y = work.bottom - h - 10;
    SetWindowPos(hWnd, NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT CALLBACK DropZoneWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_HOTKEY:
        if (wParam == 1)
            PostQuitMessage(0);
        return 0;
    case WM_DESTROY:
        DestroyGdiResources();
        PostQuitMessage(0);
        return 0;
    case WM_CLOSE:
        PostQuitMessage(0);
        return 0;
    case WM_MOUSEMOVE:
    {
        int hover = 0;
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        RECT rc;
        GetClientRect(hWnd, &rc);
        RECT rcGear, rcClose;
        GetBtnRects(&rc, &rcGear, &rcClose);
        if (PtInRect(&rcClose, pt)) hover = 2;
        else if (PtInRect(&rcGear, pt)) hover = 1;

        if (hover != g_btnHover)
        {
            g_btnHover = hover;
            InvalidateBtns(hWnd);
        }

        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
    {
        g_btnHover = 0;
        InvalidateBtns(hWnd);
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (g_btnPress)
        {
            g_btnPress = 0;
            InvalidateBtns(hWnd);
        }
        return 0;
    case WM_NCHITTEST:
    {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        ScreenToClient(hWnd, &pt);
        RECT rc;
        GetClientRect(hWnd, &rc);
        RECT rcBtnGear, rcBtnClose;
        GetBtnRects(&rc, &rcBtnGear, &rcBtnClose);
        if (PtInRect(&rcBtnGear, pt) || PtInRect(&rcBtnClose, pt))
            return HTCLIENT;
        RECT rcTitle = { 0, 0, rc.right, TITLE_H };
        if (PtInRect(&rcTitle, pt))
            return HTCAPTION;
        return HTCLIENT;
    }
    case WM_LBUTTONDOWN:
    {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        RECT rc;
        GetClientRect(hWnd, &rc);
        RECT rcGear, rcClose;
        GetBtnRects(&rc, &rcGear, &rcClose);

        int btn = 0;
        if (PtInRect(&rcClose, pt)) btn = 2;
        else if (PtInRect(&rcGear, pt)) btn = 1;

        if (btn)
        {
            g_btnPress = btn;
            SetCapture(hWnd);
            InvalidateBtns(hWnd);
        }
        return 0;
    }
    case WM_LBUTTONUP:
    {
        if (g_btnPress)
        {
            int btn = g_btnPress;
            g_btnPress = 0;
            ReleaseCapture();

            POINT pt = { LOWORD(lParam), HIWORD(lParam) };
            RECT rc;
            GetClientRect(hWnd, &rc);
            RECT rcGear, rcClose;
            GetBtnRects(&rc, &rcGear, &rcClose);

            InvalidateBtns(hWnd);

            if (btn == 2 && PtInRect(&rcClose, pt))
                PostQuitMessage(0);
            else if (btn == 1 && PtInRect(&rcGear, pt))
                DialogBoxW(g_hInst, MAKEINTRESOURCEW(IDD_SETTINGS), hWnd, SettingsDlgProc);
        }
        return 0;
    }
    case WM_PAINT:
    {
        EnsureGdiResources(hWnd);

        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);

        // title bar
        RECT rcTitle = { 0, 0, rc.right, TITLE_H };
        FillRect(hdc, &rcTitle, GetSysColorBrush(COLOR_ACTIVECAPTION));

        // title text
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, GetSysColor(COLOR_CAPTIONTEXT));
        HGDIOBJ oldFont = SelectObject(hdc, g_hTitleFont);
        RECT rcText = { 10, 0, rc.right - 2 * BTN_W, TITLE_H };
        DrawTextW(hdc, L"HashCheck", -1, &rcText,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // gear button
        RECT rcGear, rcClose;
        GetBtnRects(&rc, &rcGear, &rcClose);
        if (g_btnPress == 1)
            FillRect(hdc, &rcGear, g_brGearPress);
        else if (g_btnHover == 1)
            FillRect(hdc, &rcGear, g_brGearHover);
        SetTextColor(hdc, g_btnPress == 1 ? GetSysColor(COLOR_HIGHLIGHTTEXT) : GetSysColor(COLOR_CAPTIONTEXT));
        DrawTextW(hdc, L"⚙", -1, &rcGear,
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // close button
        if (g_btnPress == 2)
            FillRect(hdc, &rcClose, g_brClosePress);
        else if (g_btnHover == 2)
            FillRect(hdc, &rcClose, g_brCloseHover);
        SetTextColor(hdc, (g_btnHover == 2 || g_btnPress == 2) ? RGB(255,255,255) : GetSysColor(COLOR_CAPTIONTEXT));
        DrawTextW(hdc, L"✕", -1, &rcClose,
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // bottom border
        HGDIOBJ oldPen = SelectObject(hdc, g_hBorderPen);
        MoveToEx(hdc, 0, TITLE_H, NULL);
        LineTo(hdc, rc.right, TITLE_H);
        SelectObject(hdc, oldPen);

        // drop zone
        RECT rcBody = { 0, TITLE_H, rc.right, rc.bottom };
        FillRect(hdc, &rcBody, g_brBodyBg);

        oldPen = SelectObject(hdc, g_hDashPen);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        InflateRect(&rcBody, -8, -6);
        Rectangle(hdc, rcBody.left, rcBody.top, rcBody.right, rcBody.bottom);
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);

        SetTextColor(hdc, RGB(100, 100, 100));
        rcBody.left += 10; rcBody.right -= 10;
        SelectObject(hdc, g_hBodyFont);
        DrawTextW(hdc, g_dropZoneText, -1, &rcBody,
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, oldFont);

        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_DPICHANGED:
    {
        DestroyGdiResources();
        RECT* rc = (RECT*)lParam;
        SetWindowPos(hWnd, NULL, rc->left, rc->top,
                     rc->right - rc->left, rc->bottom - rc->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(hWnd, NULL, TRUE);
        return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)nCmdShow;

    g_hInst = hInstance;

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LINK_CLASS };
    InitCommonControlsEx(&icc);

    GetModuleFileNameW(NULL, g_exePath, MAX_PATH);
    wcscpy(g_exeDir, g_exePath);
    WCHAR* slash = wcsrchr(g_exeDir, L'\\');
    if (slash) *slash = 0;

    Log("--- HashCheck start --- exeDir=%S", g_exeDir);

    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD val = 0, size = sizeof(val);
        RegQueryValueExW(hKey, L"LogEnabled", NULL, NULL, (BYTE*)&val, &size);
        g_logEnabled = (val != 0);

        DWORD cacheSize = sizeof(g_cachePath);
        if (RegQueryValueExW(hKey, L"CachePath", NULL, NULL, (BYTE*)g_cachePath, &cacheSize) != ERROR_SUCCESS)
            InitDefaultCachePath();
        RegCloseKey(hKey);
    }
    else
    {
        InitDefaultCachePath();
    }

    if (FAILED(OleInitialize(NULL)))
    {
        Log("OleInitialize failed");
        CleanupTmp();
        return 1;
    }

    if (lpCmdLine[0] == 0)
    {
        WNDCLASSW wc = { 0 };
        wc.style = CS_DROPSHADOW;
        wc.lpfnWndProc = DropZoneWndProc;
        wc.hInstance = hInstance;
        wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_MAIN));
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)COLOR_WINDOW;
        wc.lpszClassName = L"HashCheckDropWindow";
        RegisterClassW(&wc);

        HWND hDrop = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                                      L"HashCheckDropWindow", L"HashCheck",
                                       WS_POPUPWINDOW,
                                      0, 0, 400, 200,
                                      NULL, NULL, hInstance, NULL);
        if (hDrop)
        {
            g_hDropWnd = hDrop;
            PositionDropZone(hDrop);
            RegisterHotKey(hDrop, 1, MOD_CONTROL | MOD_SHIFT, 'Q');
            RegisterDragDrop(hDrop, &g_dropTarget);
            ShowWindow(hDrop, SW_SHOWNOACTIVATE);
            MSG msg;
            while (GetMessageW(&msg, NULL, 0, 0))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            UnregisterHotKey(hDrop, 1);
            RevokeDragDrop(hDrop);
            DestroyWindow(hDrop);
        }
    }
    else
    {
        int argc;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argc > 1)
        {
            wcscpy(g_verifyPath, argv[1]);
            DialogBoxW(hInstance, MAKEINTRESOURCEW(IDD_VERIFY), NULL, VerifyDlgProc);
            g_verifyPath[0] = 0;
        }
        LocalFree(argv);
    }

    CleanupTmp();
    Log("--- HashCheck exit ---");
    OleUninitialize();
    return 0;
}
