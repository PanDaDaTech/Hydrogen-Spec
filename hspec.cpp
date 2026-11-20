// hspec.cpp - HSpec 轻谱 (Pure Win32 C++)
// A lightweight desktop hardware information panel.
// SPDX-License-Identifier: MIT
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <winsock2.h>
#include <windowsx.h>
#include <winioctl.h>
#include <setupapi.h>
#include <iphlpapi.h>
#include <wlanapi.h>
#include <wincrypt.h>
#include <objbase.h>
#include <propidl.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dbt.h>
#include <intrin.h>
#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>
#include <wctype.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "resource.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

#ifndef WM_DPICHANGED_AFTERPARENT
#define WM_DPICHANGED_AFTERPARENT 0x02E3
#endif
#ifndef BusTypeNvme
#define BusTypeNvme ((STORAGE_BUS_TYPE)0x11)
#endif
#ifndef BusTypeUfs
#define BusTypeUfs ((STORAGE_BUS_TYPE)0x13)
#endif

// ===========================================================================
// Small helpers
// ===========================================================================

static void WPrintf(wchar_t* dst, size_t cap, const wchar_t* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(dst, cap, _TRUNCATE, fmt, ap);
    va_end(ap);
}

static void TrimW(wchar_t* s)
{
    if (!s) return;
    wchar_t* p = s;
    while (*p == L' ' || *p == L'\t' || *p == L'\r' || *p == L'\n') p++;
    if (p != s) memmove(s, p, (wcslen(p) + 1) * sizeof(wchar_t));
    size_t n = wcslen(s);
    while (n > 0) {
        wchar_t c = s[n - 1];
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') s[--n] = 0;
        else break;
    }
}

static void ToWide(const char* s, wchar_t* dst, int cap)
{
    if (!dst || cap <= 0) return;
    dst[0] = 0;
    if (!s || !*s) return;
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, dst, cap);
    if (n <= 0) n = MultiByteToWideChar(CP_ACP, 0, s, -1, dst, cap);
    if (n <= 0) dst[0] = 0;
    else dst[cap - 1] = 0;
}

struct BrandAlias { const wchar_t* en; const wchar_t* zh; };

// Longest phrases first so "Micron Technology" wins over "Micron".
static const BrandAlias kBrandAliases[] = {
    { L"Micron Technology", L"美光" },
    { L"Western Digital", L"西部数据" },
    { L"ASUSTeK COMPUTER INC.", L"华硕" },
    { L"Micro-Star International Co., Ltd.", L"微星" },
    { L"Gigabyte Technology", L"技嘉" },
    { L"Hewlett-Packard", L"惠普" },
    { L"Intel Corporation", L"英特尔" },
    { L"SK hynix", L"海力士" },
    { L"SK Hynix", L"海力士" },
    { L"SKHynix", L"海力士" },
    { L"Hynix", L"海力士" },
    { L"Micron", L"美光" },
    { L"Samsung", L"三星" },
    { L"Kingston", L"金士顿" },
    { L"Crucial", L"英睿达" },
    { L"ADATA", L"威刚" },
    { L"Corsair", L"海盗船" },
    { L"G.Skill", L"芝奇" },
    { L"Team Group", L"十铨" },
    { L"SanDisk", L"闪迪" },
    { L"WDC", L"西部数据" },
    { L"Seagate", L"希捷" },
    { L"Toshiba", L"东芝" },
    { L"Kioxia", L"铠侠" },
    { L"ASUSTeK", L"华硕" },
    { L"ASUS", L"华硕" },
    { L"Micro-Star", L"微星" },
    { L"MSI", L"微星" },
    { L"Gigabyte", L"技嘉" },
    { L"ASRock", L"华擎" },
    { L"Lenovo", L"联想" },
    { L"Dell Inc.", L"戴尔" },
    { L"Dell", L"戴尔" },
    { L"HP", L"惠普" },
    { L"Acer", L"宏碁" },
    { L"Huawei", L"华为" },
    { L"Xiaomi", L"小米" },
    { L"Intel", L"英特尔" },
};

static wchar_t* FindNoCase(wchar_t* s, const wchar_t* find)
{
    size_t n = wcslen(find);
    if (n == 0) return s;
    for (; *s; s++) {
        size_t i = 0;
        while (i < n && s[i] &&
               towlower((wint_t)s[i]) == towlower((wint_t)find[i]))
            i++;
        if (i == n) return s;
    }
    return NULL;
}

static void ReplaceAllNoCase(wchar_t* s, int cap, const wchar_t* find,
    const wchar_t* repl)
{
    size_t fl = wcslen(find), rl = wcslen(repl);
    if (fl == 0) return;
    wchar_t* p = s;
    while ((p = FindNoCase(p, find)) != NULL) {
        size_t tail = wcslen(p + fl);
        size_t pos = (size_t)(p - s);
        if (pos + rl + tail + 1 > (size_t)cap) return;
        memmove(p + rl, p + fl, (tail + 1) * sizeof(wchar_t));
        memcpy(p, repl, rl * sizeof(wchar_t));
        p += rl;
    }
}

static void TranslateVendorText(wchar_t* s, int cap)
{
    for (size_t i = 0; i < sizeof(kBrandAliases) / sizeof(kBrandAliases[0]); i++)
        ReplaceAllNoCase(s, cap, kBrandAliases[i].en, kBrandAliases[i].zh);
}

static bool RegStrW(HKEY root, const wchar_t* sub, const wchar_t* name, wchar_t* out, DWORD outChars)
{
    if (!out || outChars == 0) return false;
    out[0] = 0;
    HKEY key = NULL;
    if (RegOpenKeyExW(root, sub, 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
    DWORD type = 0, bytes = outChars * sizeof(wchar_t);
    LONG rc = RegQueryValueExW(key, name, NULL, &type, (LPBYTE)out, &bytes);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        out[0] = 0;
        return false;
    }
    out[outChars - 1] = 0;
    TrimW(out);
    return true;
}

static bool RegDwordW(HKEY root, const wchar_t* sub, const wchar_t* name, DWORD* out)
{
    if (!out) return false;
    HKEY key = NULL;
    if (RegOpenKeyExW(root, sub, 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
    DWORD type = 0, bytes = sizeof(DWORD), value = 0;
    LONG rc = RegQueryValueExW(key, name, NULL, &type, (LPBYTE)&value, &bytes);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS || type != REG_DWORD) return false;
    *out = value;
    return true;
}

static bool RegBinaryW(HKEY root, const wchar_t* sub, const wchar_t* name,
    BYTE** out, DWORD* outSize)
{
    *out = NULL;
    *outSize = 0;
    HKEY key = NULL;
    if (RegOpenKeyExW(root, sub, 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
    DWORD type = 0, bytes = 0;
    LONG rc = RegQueryValueExW(key, name, NULL, &type, NULL, &bytes);
    if (rc != ERROR_SUCCESS || type != REG_BINARY || bytes < 8) {
        RegCloseKey(key);
        return false;
    }
    BYTE* buf = (BYTE*)malloc(bytes);
    if (!buf) {
        RegCloseKey(key);
        return false;
    }
    rc = RegQueryValueExW(key, name, NULL, &type, buf, &bytes);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS) {
        free(buf);
        return false;
    }
    *out = buf;
    *outSize = bytes;
    return true;
}

static void FormatCapacity(unsigned long long bytes, wchar_t* out, int cap)
{
    double gb = (double)bytes / 1073741824.0;
    if (gb >= 1000.0) WPrintf(out, (size_t)cap, L"%.1f TB", gb / 1024.0);
    else WPrintf(out, (size_t)cap, L"%.0f GB", gb);
}

// ===========================================================================
// SMBIOS (GetSystemFirmwareTable 'RSMB')
// ===========================================================================

struct SmbiosInfo
{
    wchar_t biosVendor[128];
    wchar_t biosVersion[192];
    wchar_t biosDate[64];

    wchar_t sysManufacturer[128];
    wchar_t sysProduct[192];
    wchar_t sysVersion[96];
    wchar_t sysSerial[96];
    wchar_t sysSku[96];

    wchar_t boardManufacturer[128];
    wchar_t boardProduct[192];
    wchar_t boardVersion[96];
    wchar_t boardSerial[96];

    wchar_t cpuVersion[192];
    int cpuMaxMhz;

    int memCount;
    struct MemModule {
        wchar_t manufacturer[64];
        wchar_t part[96];
        unsigned long long bytes;
        int speed;
        int type;
    } mem[16];
};

static void SmbiosGetStr(const BYTE* strtab, const BYTE* strend, BYTE index, wchar_t* out, int cap)
{
    out[0] = 0;
    if (index == 0 || strtab >= strend) return;
    const char* p = (const char*)strtab;
    for (BYTE i = 1; i < index; i++) {
        while (p < (const char*)strend && *p) p++;
        if (p >= (const char*)strend) return;
        p++;
    }
    if (p < (const char*)strend) {
        ToWide(p, out, cap);
        TrimW(out);
    }
}

static void SmbiosRegFallback(SmbiosInfo* si)
{
    static const wchar_t* k = L"HARDWARE\\DESCRIPTION\\System\\BIOS";
    if (!si->biosVendor[0]) RegStrW(HKEY_LOCAL_MACHINE, k, L"BIOSVendor", si->biosVendor, 128);
    if (!si->biosVersion[0]) RegStrW(HKEY_LOCAL_MACHINE, k, L"BIOSVersion", si->biosVersion, 192);
    if (!si->biosDate[0]) RegStrW(HKEY_LOCAL_MACHINE, k, L"BIOSReleaseDate", si->biosDate, 64);
    if (!si->sysManufacturer[0]) RegStrW(HKEY_LOCAL_MACHINE, k, L"SystemManufacturer", si->sysManufacturer, 128);
    if (!si->sysProduct[0]) RegStrW(HKEY_LOCAL_MACHINE, k, L"SystemProductName", si->sysProduct, 192);
    if (!si->sysVersion[0]) RegStrW(HKEY_LOCAL_MACHINE, k, L"SystemVersion", si->sysVersion, 96);
    if (!si->boardManufacturer[0]) RegStrW(HKEY_LOCAL_MACHINE, k, L"BaseBoardManufacturer", si->boardManufacturer, 128);
    if (!si->boardProduct[0]) RegStrW(HKEY_LOCAL_MACHINE, k, L"BaseBoardProduct", si->boardProduct, 192);
    if (!si->boardVersion[0]) RegStrW(HKEY_LOCAL_MACHINE, k, L"BaseBoardVersion", si->boardVersion, 96);
}

static bool SmbiosTableLooksValid(const BYTE* p, const BYTE* end)
{
    return p + 4 <= end && p[0] <= 127 && p[1] >= 4;
}

static void ParseSmbiosTable(const BYTE* table, const BYTE* end, SmbiosInfo* si)
{
    const BYTE* p = table;
    while (p + 4 <= end) {
        BYTE type = p[0];
        BYTE len = p[1];
        if (len < 4) break;
        const BYTE* fmtEnd = p + len;
        if (fmtEnd > end) break;

        const BYTE* strtab = fmtEnd;
        const BYTE* q = strtab;
        while (q + 1 < end && !(q[0] == 0 && q[1] == 0)) q++;
        const BYTE* strend = q;

        if (type == 0 && len > 0x08) {
            SmbiosGetStr(strtab, strend, p[0x04], si->biosVendor, 128);
            SmbiosGetStr(strtab, strend, p[0x05], si->biosVersion, 192);
            SmbiosGetStr(strtab, strend, p[0x08], si->biosDate, 64);
        } else if (type == 1 && len > 0x08) {
            SmbiosGetStr(strtab, strend, p[0x04], si->sysManufacturer, 128);
            SmbiosGetStr(strtab, strend, p[0x05], si->sysProduct, 192);
            SmbiosGetStr(strtab, strend, p[0x06], si->sysVersion, 96);
            SmbiosGetStr(strtab, strend, p[0x07], si->sysSerial, 96);
            if (len > 0x1A) SmbiosGetStr(strtab, strend, p[0x19], si->sysSku, 96);
        } else if (type == 2 && len > 0x08) {
            SmbiosGetStr(strtab, strend, p[0x04], si->boardManufacturer, 128);
            SmbiosGetStr(strtab, strend, p[0x05], si->boardProduct, 192);
            SmbiosGetStr(strtab, strend, p[0x06], si->boardVersion, 96);
            SmbiosGetStr(strtab, strend, p[0x07], si->boardSerial, 96);
        } else if (type == 4 && len > 0x25) {
            SmbiosGetStr(strtab, strend, p[0x10], si->cpuVersion, 192);
            si->cpuMaxMhz = (int)*(WORD*)(p + 0x14);
        } else if (type == 17 && len >= 0x17 && si->memCount < 16) {
            unsigned long long memBytes = 0;
            WORD sizeMb = *(WORD*)(p + 0x0C);
            if (sizeMb == 0x7FFF && len >= 0x20) {
                DWORD extMb = *(DWORD*)(p + 0x1C) & 0x7FFFFFFF;
                memBytes = (unsigned long long)extMb * 1024ULL * 1024ULL;
            } else if (sizeMb > 0 && sizeMb != 0xFFFF) {
                memBytes = (unsigned long long)sizeMb * 1024ULL * 1024ULL;
            }
            if (memBytes > 0) {
                SmbiosInfo::MemModule& m = si->mem[si->memCount];
                memset(&m, 0, sizeof(m));
                m.bytes = memBytes;
                m.type = (len > 0x12) ? p[0x12] : 0;
                m.speed = (int)*(WORD*)(p + 0x15);
                if (m.speed == 0xFFFF && len >= 0x58) {
                    // SMBIOS 3.2 extended speed lives at 0x54.
                    DWORD extSpeed = *(DWORD*)(p + 0x54);
                    if (extSpeed > 0 && extSpeed != 0xFFFFFFFF) m.speed = (int)extSpeed;
                }
                if (len > 0x17) SmbiosGetStr(strtab, strend, p[0x17], m.manufacturer, 64);
                if (len > 0x1A) SmbiosGetStr(strtab, strend, p[0x1A], m.part, 96);
                si->memCount++;
            }
        }

        p = (q + 1 < end) ? q + 2 : end;
    }
}

static bool ParseSmbios(SmbiosInfo* si)
{
    memset(si, 0, sizeof(*si));
    bool parsed = false;

    DWORD size = GetSystemFirmwareTable('RSMB', 0, NULL, 0);
    if (size >= 8) {
        BYTE* buf = (BYTE*)malloc(size);
        if (buf) {
            DWORD got = GetSystemFirmwareTable('RSMB', 0, buf, size);
            if (got >= 8) {
                DWORD len = *(DWORD*)(buf + 4);
                const BYTE* table = buf + 8;
                const BYTE* end = table + len;
                if (end > buf + got) end = buf + got;
                if (SmbiosTableLooksValid(table, end)) {
                    ParseSmbiosTable(table, end, si);
                    parsed = true;
                }
            }
            free(buf);
        }
    }

    if (!parsed) {
        // Some systems reject the firmware-table API; mssmbios keeps the same
        // RawSMBIOSData blob in the registry.
        BYTE* reg = NULL;
        DWORD regSize = 0;
        if (RegBinaryW(HKEY_LOCAL_MACHINE,
                L"SYSTEM\\CurrentControlSet\\Services\\mssmbios\\Data",
                L"SMBiosData", &reg, &regSize)) {
            DWORD len = (regSize >= 8) ? *(DWORD*)(reg + 4) : 0;
            if (len > 0 && 8 + len <= regSize &&
                SmbiosTableLooksValid(reg + 8, reg + 8 + len)) {
                ParseSmbiosTable(reg + 8, reg + 8 + len, si);
                parsed = true;
            } else if (SmbiosTableLooksValid(reg, reg + regSize)) {
                ParseSmbiosTable(reg, reg + regSize, si);
                parsed = true;
            }
            free(reg);
        }
    }

    SmbiosRegFallback(si);
    return parsed;
}

static const wchar_t* MemTypeName(int type)
{
    switch (type) {
    case 0x03: return L"DRAM";
    case 0x0F: return L"SDRAM";
    case 0x12: return L"DDR";
    case 0x13: return L"DDR2";
    case 0x18: return L"DDR3";
    case 0x1A: return L"DDR4";
    case 0x1B: return L"LPDDR";
    case 0x1C: return L"LPDDR2";
    case 0x1D: return L"LPDDR3";
    case 0x1E: return L"LPDDR4";
    case 0x1F: return L"NVDIMM";
    case 0x20: return L"HBM";
    case 0x21: return L"HBM2";
    case 0x22: return L"DDR5";
    case 0x23: return L"LPDDR5";
    default: return NULL;
    }
}

// ===========================================================================
// CPU
// ===========================================================================

static void GetCpuBrand(wchar_t* out, int cap)
{
    out[0] = 0;
    RegStrW(HKEY_LOCAL_MACHINE,
        L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
        L"ProcessorNameString", out, (DWORD)cap);
    TrimW(out);
#if defined(_M_IX86) || defined(_M_X64)
    if (!out[0]) {
        int regs[4] = { 0 };
        __cpuid(regs, 0x80000000);
        if ((unsigned)regs[0] >= 0x80000004) {
            char brand[49] = { 0 };
            for (int i = 0; i < 3; i++) {
                __cpuid(regs, 0x80000002 + i);
                memcpy(brand + i * 16, regs, 16);
            }
            ToWide(brand, out, cap);
            TrimW(out);
        }
    }
#endif
}

static int GetPhysicalCores(void)
{
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, NULL, &len);
    if (len == 0) return 0;
    BYTE* buf = (BYTE*)malloc(len);
    if (!buf) return 0;
    int count = 0;
    if (GetLogicalProcessorInformationEx(RelationProcessorCore,
            (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)buf, &len)) {
        DWORD off = 0;
        while (off + 8 <= len) {
            SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* e =
                (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)(buf + off);
            if (e->Relationship == RelationProcessorCore) count++;
            if (e->Size == 0) break;
            off += e->Size;
        }
    }
    free(buf);
    return count;
}

static int GetLogicalCpus(void)
{
    typedef DWORD (WINAPI *PFN_GetActiveProcessorCount)(WORD);
    HMODULE k = GetModuleHandleW(L"kernel32.dll");
    if (k) {
        PFN_GetActiveProcessorCount fn =
            (PFN_GetActiveProcessorCount)GetProcAddress(k, "GetActiveProcessorCount");
        if (fn) {
            DWORD n = fn(0xFFFF); // ALL_PROCESSOR_GROUPS
            if (n > 0) return (int)n;
        }
    }
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int)si.dwNumberOfProcessors;
}

// ===========================================================================
// Firmware / Secure Boot
// ===========================================================================

static bool EnablePrivilege(const wchar_t* name)
{
    HANDLE token = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
        return false;
    bool ok = false;
    LUID luid;
    if (LookupPrivilegeValueW(NULL, name, &luid)) {
        TOKEN_PRIVILEGES tp;
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        SetLastError(ERROR_SUCCESS);
        ok = AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), NULL, NULL) != FALSE
            && GetLastError() == ERROR_SUCCESS;
    }
    CloseHandle(token);
    return ok;
}

// 1 = BIOS/Legacy, 2 = UEFI, 0 = unknown
static int GetFirmwareKind(void)
{
    typedef BOOL (WINAPI *PFN_GetFirmwareType)(int*);
    HMODULE k = GetModuleHandleW(L"kernel32.dll");
    if (k) {
        PFN_GetFirmwareType fn = (PFN_GetFirmwareType)GetProcAddress(k, "GetFirmwareType");
        int ft = 0;
        if (fn && fn(&ft)) return ft;
    }
    DWORD v = 0;
    if (RegDwordW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control",
            L"PEFirmwareType", &v))
        return (int)v;
    return 0;
}

// 1 = on, 0 = off, -1 = unknown
static int GetSecureBootState(void)
{
    DWORD v = 0;
    if (RegDwordW(HKEY_LOCAL_MACHINE,
            L"SYSTEM\\CurrentControlSet\\Control\\SecureBoot\\State",
            L"UEFISecureBootEnabled", &v))
        return v ? 1 : 0;

    if (EnablePrivilege(L"SeSystemEnvironmentPrivilege")) {
        BYTE b = 0;
        DWORD n = GetFirmwareEnvironmentVariableW(L"SecureBoot",
            L"{8be4df61-93ca-11d2-aa0d-00e098032b8c}", &b, 1);
        if (n == 1) return b ? 1 : 0;
    }
    return -1;
}

static const GUID kEfiCertX509 =
    { 0xa5c059a1, 0x94e4, 0x4aa7, { 0x87, 0xb5, 0xab, 0x15, 0x5c, 0x2b, 0xf0, 0x72 } };

// Returns the currently installed Secure Boot CA generation (2023 preferred,
// otherwise 2011). Only the allowed-signature database (db) is inspected;
// dbx contains revoked certificates and must not be reported as current.
static bool CollectBootCert(wchar_t* out, int cap)
{
    if (!out || cap <= 0) return false;
    out[0] = 0;

    bool has2011 = false, has2023 = false;
    wchar_t other[64] = { 0 };

    if (EnablePrivilege(L"SeSystemEnvironmentPrivilege")) {
        const DWORD kBufSize = 128 * 1024;
        BYTE* buf = (BYTE*)malloc(kBufSize);
        if (buf) {
            SetLastError(ERROR_SUCCESS);
            DWORD n = GetFirmwareEnvironmentVariableW(L"db",
                L"{8be4df61-93ca-11d2-aa0d-00e098032b8c}", buf, kBufSize);
            if (n > 0 && n <= kBufSize) {
                DWORD off = 0;
                while (off + 28 <= n) {
                    const BYTE* list = buf + off;
                    UINT32 listSize = *(UINT32*)(list + 16);
                    UINT32 hdrSize = *(UINT32*)(list + 20);
                    UINT32 sigSize = *(UINT32*)(list + 24);
                    if (listSize < 28 || off + listSize > n || sigSize == 0) break;

                    GUID type;
                    memcpy(&type, list, sizeof(GUID));
                    DWORD p = off + 28 + hdrSize;
                    DWORD listEnd = off + listSize;

                    while (p + sigSize <= listEnd) {
                        if (IsEqualGUID(type, kEfiCertX509)) {
                            PCCERT_CONTEXT cert = CertCreateCertificateContext(
                                X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, buf + p, sigSize);
                            if (cert) {
                                wchar_t cn[256] = { 0 };
                                CertGetNameStringW(cert, CERT_NAME_SIMPLE_DISPLAY_TYPE,
                                    0, NULL, cn, 256);
                                if (wcsstr(cn, L"2023")) has2023 = true;
                                else if (wcsstr(cn, L"2011")) has2011 = true;
                                else if (!other[0] && cn[0])
                                    wcsncpy_s(other, 64, cn, _TRUNCATE);
                                CertFreeCertificateContext(cert);
                            }
                        }
                        p += sigSize;
                    }
                    off += listSize;
                }
            }
            free(buf);
        }
    }

    if (!has2011 && !has2023) {
        // Firmware variables unavailable: use the Windows CA servicing state.
        wchar_t status[64] = { 0 };
        if (RegStrW(HKEY_LOCAL_MACHINE,
                L"SYSTEM\\CurrentControlSet\\Control\\SecureBoot\\Servicing",
                L"UEFICA2023Status", status, 64)) {
            if (_wcsicmp(status, L"Updated") == 0 ||
                _wcsicmp(status, L"Installed") == 0 ||
                _wcsicmp(status, L"Applied") == 0 ||
                _wcsicmp(status, L"Success") == 0)
                has2023 = true;
        }
        DWORD enabled = 0;
        if (!has2023 &&
            RegDwordW(HKEY_LOCAL_MACHINE,
                L"SYSTEM\\CurrentControlSet\\Control\\SecureBoot\\State",
                L"UEFISecureBootEnabled", &enabled) && enabled)
            has2011 = true;
    }

    if (has2011 && has2023) wcscpy_s(out, (size_t)cap, L"PCA 2011/CA 2023");
    else if (has2011) wcscpy_s(out, (size_t)cap, L"PCA 2011");
    else if (has2023) wcscpy_s(out, (size_t)cap, L"CA 2023");
    else if (other[0]) wcsncpy_s(out, (size_t)cap, other, _TRUNCATE);
    else return false;
    return true;
}

// ===========================================================================
// Disks
// ===========================================================================

struct DiskInfo
{
    wchar_t model[160];
    wchar_t bus[16];
    wchar_t style[8];
    unsigned long long bytes;
};

struct DiskVendor { const wchar_t* en; const wchar_t* zh; };

static const DiskVendor kDiskVendors[] = {
    { L"SK hynix", L"海力士" },
    { L"SK Hynix", L"海力士" },
    { L"SK_Hynix", L"海力士" },
    { L"SKHynix", L"海力士" },
    { L"Hynix", L"海力士" },
    { L"Samsung", L"三星" },
    { L"Seagate", L"希捷" },
    { L"Western Digital", L"西部数据" },
    { L"WDC", L"西部数据" },
    { L"Toshiba", L"东芝" },
    { L"Kioxia", L"铠侠" },
    { L"Kingston", L"金士顿" },
    { L"Crucial", L"英睿达" },
    { L"Micron", L"美光" },
    { L"SanDisk", L"闪迪" },
    { L"Intel", L"英特尔" },
    { L"Lenovo", L"联想" },
    { L"LITEON", L"建兴" },
    { L"Plextor", L"浦科特" },
    { L"Netac", L"朗科" },
    { L"Teclast", L"台电" },
};

// Localize the leading vendor token of a disk model string.
static void LocalizeDiskVendor(wchar_t* model)
{
    for (size_t i = 0; i < sizeof(kDiskVendors) / sizeof(kDiskVendors[0]); i++) {
        const wchar_t* key = kDiskVendors[i].en;
        size_t n = wcslen(key);
        if (_wcsnicmp(model, key, n) != 0) continue;
        const wchar_t* rest = model + n;
        while (*rest == L'_' || *rest == L' ' || *rest == L'-') rest++;
        wchar_t tmp[160];
        if (*rest)
            WPrintf(tmp, 160, L"%s %s", kDiskVendors[i].zh, rest);
        else
            wcscpy_s(tmp, 160, kDiskVendors[i].zh);
        wcsncpy_s(model, 160, tmp, _TRUNCATE);
        return;
    }
}

static const wchar_t* BusTypeName(STORAGE_BUS_TYPE t)
{
    switch (t) {
    case BusTypeScsi: return L"SCSI";
    case BusTypeAtapi: return L"ATAPI";
    case BusTypeAta: return L"ATA";
    case BusType1394: return L"1394";
    case BusTypeSsa: return L"SSA";
    case BusTypeFibre: return L"FIBRE";
    case BusTypeUsb: return L"USB";
    case BusTypeRAID: return L"RAID";
    case BusTypeiScsi: return L"ISCSI";
    case BusTypeSas: return L"SAS";
    case BusTypeSata: return L"SATA";
    case BusTypeSd: return L"SD";
    case BusTypeMmc: return L"MMC";
    case BusTypeVirtual: return L"VIRTUAL";
    case BusTypeFileBackedVirtual: return L"VIRTUAL";
    case BusTypeSpaces: return L"SPACES";
    case BusTypeNvme: return L"NVME";
    case BusTypeUfs: return L"UFS";
    default: return L"UNKNOWN";
    }
}

static int EnumDisks(DiskInfo* out, int cap)
{
    int count = 0;
    for (int i = 0; i < 16 && count < cap; i++) {
        wchar_t path[64];
        WPrintf(path, 64, L"\\\\.\\PhysicalDrive%d", i);
        // Access 0 is enough: geometry / property / layout IOCTLs are
        // FILE_ANY_ACCESS, so the panel works without elevation.
        HANDLE h = CreateFileW(path, 0,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) continue;

        DiskInfo di;
        memset(&di, 0, sizeof(di));
        DWORD ret = 0;

        BYTE geoBuf[4096];
        memset(geoBuf, 0, sizeof(geoBuf));
        if (!DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0,
                geoBuf, sizeof(geoBuf), &ret, NULL)) {
            // Elevated fallback, e.g. unusual storage drivers.
            GET_LENGTH_INFORMATION len;
            memset(&len, 0, sizeof(len));
            if (!DeviceIoControl(h, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0,
                    &len, sizeof(len), &ret, NULL)) {
                CloseHandle(h);
                continue;
            }
            di.bytes = (unsigned long long)len.Length.QuadPart;
        } else {
            DISK_GEOMETRY_EX* geo = (DISK_GEOMETRY_EX*)geoBuf;
            di.bytes = (unsigned long long)geo->DiskSize.QuadPart;
        }
        if (di.bytes == 0) { CloseHandle(h); continue; }

        BYTE descBuf[1024];
        memset(descBuf, 0, sizeof(descBuf));
        STORAGE_PROPERTY_QUERY q;
        memset(&q, 0, sizeof(q));
        q.PropertyId = StorageDeviceProperty;
        q.QueryType = PropertyStandardQuery;
        if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q),
                descBuf, sizeof(descBuf), &ret, NULL)) {
            STORAGE_DEVICE_DESCRIPTOR* d = (STORAGE_DEVICE_DESCRIPTOR*)descBuf;
            wcscpy_s(di.bus, 16, BusTypeName(d->BusType));
            wchar_t v[96] = { 0 }, p[128] = { 0 };
            if (d->VendorIdOffset != 0 && d->VendorIdOffset < sizeof(descBuf))
                ToWide((const char*)descBuf + d->VendorIdOffset, v, 96);
            if (d->ProductIdOffset != 0 && d->ProductIdOffset < sizeof(descBuf))
                ToWide((const char*)descBuf + d->ProductIdOffset, p, 128);
            TrimW(v); TrimW(p);
            if (p[0]) {
                if (v[0] && wcsstr(p, v) == NULL)
                    WPrintf(di.model, 160, L"%s %s", v, p);
                else
                    wcsncpy_s(di.model, 160, p, _TRUNCATE);
            } else {
                wcsncpy_s(di.model, 160, v, _TRUNCATE);
            }
        }
        if (!di.bus[0]) wcscpy_s(di.bus, 16, L"UNKNOWN");

        BYTE layoutBuf[16384];
        memset(layoutBuf, 0, sizeof(layoutBuf));
        if (DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0,
                layoutBuf, sizeof(layoutBuf), &ret, NULL)) {
            DWORD style = *(DWORD*)layoutBuf;
            if (style == PARTITION_STYLE_GPT) wcscpy_s(di.style, 8, L"GPT");
            else if (style == PARTITION_STYLE_MBR) wcscpy_s(di.style, 8, L"MBR");
            else wcscpy_s(di.style, 8, L"RAW");
        } else {
            wcscpy_s(di.style, 8, L"UNKNOWN");
        }

        if (!di.model[0]) wcscpy_s(di.model, 160, L"Disk");
        LocalizeDiskVendor(di.model);
        out[count++] = di;
        CloseHandle(h);
    }
    return count;
}

// ===========================================================================
// GPU / network (SetupAPI + embedded PCI ID tables)
// ===========================================================================

struct IdEntry { wchar_t key[9]; wchar_t name[128]; };
static IdEntry g_gpuIds[4096];
static int g_gpuIdCount = 0;
static IdEntry g_nicIds[3072];
static int g_nicIdCount = 0;

static void LoadIdTable(HINSTANCE inst, int resId, IdEntry* table, int cap, int* count)
{
    *count = 0;
    HRSRC hr = FindResourceW(inst, MAKEINTRESOURCEW(resId), MAKEINTRESOURCEW(10));
    if (!hr) return;
    HGLOBAL hg = LoadResource(inst, hr);
    if (!hg) return;
    const char* p = (const char*)LockResource(hg);
    DWORD sz = SizeofResource(inst, hr);
    if (!p || sz == 0) return;
    const char* end = p + sz;
    int n = 0;
    while (p < end && n < cap) {
        const char* eol = p;
        while (eol < end && *eol != '\n') eol++;
        const char* eq = p;
        while (eq < eol && *eq != '=') eq++;
        if (eq < eol && (eq - p) == 8) {
            for (int k = 0; k < 8; k++) table[n].key[k] = (wchar_t)p[k];
            table[n].key[8] = 0;
            int len = (int)(eol - (eq + 1));
            if (len > 0 && eq[len] == '\r') len--;
            if (len > 255) len = 255;
            char tmp[256];
            memcpy(tmp, eq + 1, len);
            tmp[len] = 0;
            ToWide(tmp, table[n].name, 128);
            if (table[n].name[0]) n++;
        }
        p = (eol < end) ? eol + 1 : end;
    }
    *count = n;
}

static void LoadIdTables(HINSTANCE inst)
{
    LoadIdTable(inst, IDR_GPU_IDS, g_gpuIds, 4096, &g_gpuIdCount);
    LoadIdTable(inst, IDR_NIC_IDS, g_nicIds, 3072, &g_nicIdCount);
}

static bool LookupIdTable(const IdEntry* table, int count, const wchar_t* key,
    wchar_t* out, int cap)
{
    if (!key || wcslen(key) != 8) return false;
    for (int i = 0; i < count; i++) {
        if (wcscmp(table[i].key, key) == 0) {
            wcsncpy_s(out, (size_t)cap, table[i].name, _TRUNCATE);
            return true;
        }
    }
    return false;
}

static bool ExtractPciKey(const wchar_t* id, wchar_t* key)
{
    const wchar_t* v = FindNoCase((wchar_t*)id, L"VEN_");
    const wchar_t* d = FindNoCase((wchar_t*)id, L"DEV_");
    if (!v || !d) return false;
    for (int i = 0; i < 4; i++) {
        wchar_t a = v[4 + i], b = d[4 + i];
        if (!iswxdigit(a) || !iswxdigit(b)) return false;
        key[i] = (wchar_t)towupper(a);
        key[4 + i] = (wchar_t)towupper(b);
    }
    key[8] = 0;
    return true;
}

static void GetDeviceString(HDEVINFO devs, SP_DEVINFO_DATA* data, DWORD prop,
    wchar_t* out, int cap)
{
    out[0] = 0;
    wchar_t buf[512] = { 0 };
    DWORD type = 0, size = 0;
    if (SetupDiGetDeviceRegistryPropertyW(devs, data, prop, &type,
            (PBYTE)buf, sizeof(buf), &size))
        wcsncpy_s(out, (size_t)cap, buf, _TRUNCATE);
}

static const wchar_t* GpuVendorFallback(const wchar_t* key)
{
    if (!_wcsnicmp(key, L"10DE", 4)) return L"NVIDIA GPU";
    if (!_wcsnicmp(key, L"1002", 4)) return L"AMD GPU";
    if (!_wcsnicmp(key, L"8086", 4)) return L"Intel GPU";
    if (!_wcsnicmp(key, L"15AD", 4)) return L"VMware GPU";
    return L"显卡";
}

struct GpuInfo { wchar_t name[192]; wchar_t key[9]; int rank; };

static int GpuRank(const wchar_t* name)
{
    static const wchar_t* discrete[] = {
        L"GeForce", L"Radeon RX", L"Radeon Pro", L"Quadro", L"RTX", L"GTX", L"Arc"
    };
    for (int i = 0; i < 7; i++)
        if (FindNoCase((wchar_t*)name, discrete[i])) return 0;
    return 1;
}

// WinPE marker: HKLM\SYSTEM\CurrentControlSet\Control\MiniNT exists.
static bool IsWinPE(void)
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\CurrentControlSet\Control\MiniNT",
            0, KEY_READ, &k) == ERROR_SUCCESS) {
        RegCloseKey(k);
        return true;
    }
    return false;
}

// Index of the GPU driving the desktop (its display output is attached),
// matched back through the PCI vendor/device key.  Returns -1 when there is
// no desktop to ask (WinPE, headless) - the caller then prefers the discrete
// adapter.
static int GpuActiveIndex(const GpuInfo* gpus, int gc)
{
    if (IsWinPE()) return -1;
    DISPLAY_DEVICEW dd;
    memset(&dd, 0, sizeof(dd));
    dd.cb = sizeof(dd);
    for (int i = 0; EnumDisplayDevicesW(NULL, i, &dd, 0); i++) {
        if (!(dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)) continue;
        wchar_t key[9] = { 0 };
        if (!ExtractPciKey(dd.DeviceID, key)) continue;
        for (int j = 0; j < gc; j++)
            if (wcscmp(gpus[j].key, key) == 0) return j;
    }
    return -1;
}

// WinOSInfo-style graphics header: monitor model, size, mode and refresh.
struct DisplayInfo {
    wchar_t name[128];
    double diagIn;        // 0 when unknown
    int w, h, hz;
    bool valid;
};

static void EdidProductName(const BYTE* edid, DWORD size, wchar_t* out, int cap)
{
    out[0] = 0;
    if (size < 126) return;
    for (int d = 0; d < 4; d++) {
        const BYTE* p = edid + 54 + d * 18;
        if (p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 0xFC) {
            char name[14] = { 0 };
            for (int i = 0; i < 13; i++) {
                BYTE c = p[5 + i];
                if (c == 0x0A || c == 0x00) break;
                name[i] = (char)c;
            }
            MultiByteToWideChar(CP_ACP, 0, name, -1, out, cap);
            TrimW(out);
            return;
        }
    }
}

static bool GetDisplayInfo(DisplayInfo* out)
{
    memset(out, 0, sizeof(*out));
    DISPLAY_DEVICEW adapter;
    memset(&adapter, 0, sizeof(adapter));
    adapter.cb = sizeof(adapter);
    for (int i = 0; EnumDisplayDevicesW(NULL, i, &adapter, 0); i++) {
        if (!(adapter.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)) continue;

        DEVMODEW dm;
        memset(&dm, 0, sizeof(dm));
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsW(adapter.DeviceName, ENUM_CURRENT_SETTINGS, &dm)) {
            out->w = (int)dm.dmPelsWidth;
            out->h = (int)dm.dmPelsHeight;
            out->hz = (int)dm.dmDisplayFrequency;
            if (out->hz <= 1) out->hz = 0;
        }

        DISPLAY_DEVICEW mon;
        memset(&mon, 0, sizeof(mon));
        mon.cb = sizeof(mon);
        if (EnumDisplayDevicesW(adapter.DeviceName, 0, &mon, 0)) {
            if (mon.DeviceString[0] && !FindNoCase(mon.DeviceString, L"Generic PnP"))
                wcsncpy_s(out->name, 128, mon.DeviceString, _TRUNCATE);

            // EDID lives under the monitor's Enum key (DeviceID minus the
            // class-GUID tail) and gives the product name plus panel size.
            wchar_t id[256];
            wcsncpy_s(id, 256, mon.DeviceID, _TRUNCATE);
            wchar_t* guid = wcsstr(id, L"\\{");
            if (guid) *guid = 0;
            wchar_t sub[320];
            WPrintf(sub, 320, L"SYSTEM\\CurrentControlSet\\Enum\\%s\\Device Parameters", id);
            HKEY key = NULL;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, sub, 0, KEY_READ, &key) == ERROR_SUCCESS) {
                BYTE edid[256];
                DWORD type = 0, size = sizeof(edid);
                if (RegQueryValueExW(key, L"EDID", NULL, &type, edid, &size) == ERROR_SUCCESS) {
                    if (!out->name[0]) EdidProductName(edid, size, out->name, 128);
                    if (size >= 23 && edid[21] > 0 && edid[22] > 0) {
                        double cw = edid[21], ch = edid[22];
                        out->diagIn = sqrt(cw * cw + ch * ch) / 2.54;
                    }
                }
                RegCloseKey(key);
            }
        }
        out->valid = (out->w > 0);
        return out->valid;
    }
    return false;
}

// Dedicated VRAM from the display-class registry key, matched back through
// the PCI vendor/device id.  0 when the driver does not report it.
static unsigned long long GpuVramBytes(const wchar_t* key8)
{
    if (!key8 || !key8[0]) return 0;
    wchar_t needle[32];
    WPrintf(needle, 32, L"VEN_%c%c%c%c&DEV_%c%c%c%c",
        key8[0], key8[1], key8[2], key8[3], key8[4], key8[5], key8[6], key8[7]);
    HKEY root = NULL;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}",
            0, KEY_READ, &root) != ERROR_SUCCESS)
        return 0;
    unsigned long long best = 0;
    for (int i = 0; i < 24 && !best; i++) {
        wchar_t name[8];
        WPrintf(name, 8, L"%04d", i);
        HKEY sub = NULL;
        if (RegOpenKeyExW(root, name, 0, KEY_READ, &sub) != ERROR_SUCCESS) continue;
        wchar_t mid[256] = { 0 };
        DWORD type = 0, size = sizeof(mid);
        if (RegQueryValueExW(sub, L"MatchingDeviceId", NULL, &type, (LPBYTE)mid, &size) == ERROR_SUCCESS
                && FindNoCase(mid, needle)) {
            unsigned long long v = 0;
            size = sizeof(v);
            if (RegQueryValueExW(sub, L"HardwareInformation.qwMemorySize", NULL, &type,
                    (LPBYTE)&v, &size) == ERROR_SUCCESS && size == 8)
                best = v;
        }
        RegCloseKey(sub);
    }
    RegCloseKey(root);
    return best;
}

static void FormatVram(unsigned long long bytes, wchar_t* out, int cap)
{
    out[0] = 0;
    if (bytes == 0) return;
    if (bytes >= (1ull << 30))
        WPrintf(out, cap, L"%lluGB", (bytes + (1ull << 29)) >> 30);
    else
        WPrintf(out, cap, L"%lluMB", (bytes + (1ull << 19)) >> 20);
}

static int EnumGpus(GpuInfo* out, int cap)
{
    GUID guid = { 0x4d36e968, 0xe325, 0x11ce,
        { 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 } };
    HDEVINFO devs = SetupDiGetClassDevsW(&guid, NULL, NULL, DIGCF_PRESENT);
    if (devs == INVALID_HANDLE_VALUE) return 0;

    int n = 0;
    SP_DEVINFO_DATA data;
    data.cbSize = sizeof(data);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(devs, i, &data); i++) {
        wchar_t id[512] = { 0 };
        if (!SetupDiGetDeviceInstanceIdW(devs, &data, id, 512, NULL)) continue;
        wchar_t key[9] = { 0 };
        bool hasKey = ExtractPciKey(id, key);
        wchar_t name[192] = { 0 };
        if (hasKey) LookupIdTable(g_gpuIds, g_gpuIdCount, key, name, 192);
        if (!name[0]) {
            GetDeviceString(devs, &data, SPDRP_FRIENDLYNAME, name, 192);
            if (!name[0]) GetDeviceString(devs, &data, SPDRP_DEVICEDESC, name, 192);
        }
        if (!name[0]) continue;
        if (FindNoCase(name, L"Basic Render Driver")) continue;
        if (FindNoCase(name, L"Microsoft Basic") && hasKey)
            wcsncpy_s(name, 192, GpuVendorFallback(key), _TRUNCATE);

        static const wchar_t* kGpuVirtual[] = {
            L"Virtual Display", L"GameViewer", L"Spacedesk", L"Parsec",
            L"Sunshine", L"Virtual Desktop", L"Mirror Driver",
            L"Indirect Display"
        };
        bool virt = false;
        for (int k = 0; k < 8; k++) {
            if (FindNoCase(name, kGpuVirtual[k])) { virt = true; break; }
        }
        if (virt) continue;

        bool dup = false;
        for (int j = 0; j < n; j++) {
            if ((hasKey && wcscmp(out[j].key, key) == 0) ||
                wcscmp(out[j].name, name) == 0) {
                dup = true;
                break;
            }
        }
        if (dup) continue;

        wcsncpy_s(out[n].name, 192, name, _TRUNCATE);
        wcsncpy_s(out[n].key, 9, key, _TRUNCATE);
        out[n].rank = GpuRank(name);
        n++;
        if (n >= cap) break;
    }
    SetupDiDestroyDeviceInfoList(devs);

    // Discrete GPUs first.
    for (int i = 1; i < n; i++) {
        GpuInfo tmp = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].rank > tmp.rank) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = tmp;
    }
    return n;
}

struct NicInfo {
    wchar_t name[192];
    wchar_t guid[64];
    wchar_t mac[32];
    wchar_t ip[64];
    wchar_t gateway[64];
    wchar_t ssid[64];
    unsigned long long speedBps;
    bool wireless;
    bool connected;
};

static bool NameHasAny(const wchar_t* s, const wchar_t* const* words, int n)
{
    for (int i = 0; i < n; i++)
        if (FindNoCase((wchar_t*)s, words[i])) return true;
    return false;
}

static void FormatMac(const BYTE* p, int n, wchar_t* out, int cap)
{
    out[0] = 0;
    if (!p || n <= 0 || n > 8) return;
    wchar_t buf[32] = { 0 };
    int pos = 0;
    for (int i = 0; i < n; i++) {
        int w = (i == 0)
            ? _snwprintf_s(buf + pos, 32 - pos, _TRUNCATE, L"%02X", p[i])
            : _snwprintf_s(buf + pos, 32 - pos, _TRUNCATE, L"-%02X", p[i]);
        if (w < 0) break;
        pos += w;
    }
    wcsncpy_s(out, (size_t)cap, buf, _TRUNCATE);
}

static void FormatIpv4(const SOCKADDR* sa, wchar_t* out, int cap)
{
    out[0] = 0;
    if (!sa || sa->sa_family != AF_INET) return;
    const SOCKADDR_IN* sin = (const SOCKADDR_IN*)sa;
    const BYTE* b = (const BYTE*)&sin->sin_addr;
    WPrintf(out, (size_t)cap, L"%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
}

static void FormatSpeed(unsigned long long bps, wchar_t* out, int cap)
{
    out[0] = 0;
    if (bps >= 1000000000ULL)
        WPrintf(out, (size_t)cap, L"%.1f Gbps", (double)bps / 1000000000.0);
    else if (bps >= 1000000ULL)
        WPrintf(out, (size_t)cap, L"%.1f Mbps", (double)bps / 1000000.0);
    else if (bps >= 1000ULL)
        WPrintf(out, (size_t)cap, L"%.1f Kbps", (double)bps / 1000.0);
}

static void FillNicNetwork(NicInfo* nics, int count)
{
    const ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST |
        GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG size = 0;
    ULONG rc = GetAdaptersAddresses(AF_UNSPEC, flags, NULL, NULL, &size);
    if (rc != ERROR_BUFFER_OVERFLOW && rc != NO_ERROR) return;
    IP_ADAPTER_ADDRESSES* addrs = (IP_ADAPTER_ADDRESSES*)malloc(size);
    if (!addrs) return;
    if (GetAdaptersAddresses(AF_UNSPEC, flags, NULL, addrs, &size) != NO_ERROR) {
        free(addrs);
        return;
    }

    for (IP_ADAPTER_ADDRESSES* a = addrs; a; a = a->Next) {
        wchar_t aguid[128] = { 0 };
        if (a->AdapterName)
            MultiByteToWideChar(CP_ACP, 0, a->AdapterName, -1, aguid, 128);

        for (int i = 0; i < count; i++) {
            NicInfo& n = nics[i];
            if (!n.guid[0] || _wcsicmp(n.guid, aguid) != 0) continue;

            if (a->PhysicalAddressLength > 0 && a->PhysicalAddressLength <= 8)
                FormatMac(a->PhysicalAddress, (int)a->PhysicalAddressLength, n.mac, 32);
            if (a->TransmitLinkSpeed > n.speedBps)
                n.speedBps = a->TransmitLinkSpeed;

            for (IP_ADAPTER_UNICAST_ADDRESS* u = a->FirstUnicastAddress; u; u = u->Next) {
                if (!u->Address.lpSockaddr ||
                    u->Address.lpSockaddr->sa_family != AF_INET) continue;
                wchar_t ip[64] = { 0 };
                FormatIpv4(u->Address.lpSockaddr, ip, 64);
                if (!ip[0]) continue;
                bool linkLocal = wcsncmp(ip, L"169.254.", 8) == 0;
                bool haveLinkLocal = wcsncmp(n.ip, L"169.254.", 8) == 0;
                if (!n.ip[0] || (haveLinkLocal && !linkLocal))
                    wcsncpy_s(n.ip, 64, ip, _TRUNCATE);
            }
            for (IP_ADAPTER_GATEWAY_ADDRESS* g = a->FirstGatewayAddress; g; g = g->Next) {
                if (!g->Address.lpSockaddr ||
                    g->Address.lpSockaddr->sa_family != AF_INET) continue;
                FormatIpv4(g->Address.lpSockaddr, n.gateway, 64);
                if (n.gateway[0]) break;
            }
            if (a->IfType == IF_TYPE_IEEE80211) n.wireless = true;
            if (a->OperStatus == IfOperStatusUp) n.connected = true;
            break;
        }
    }
    free(addrs);
}

static void FillNicSsids(NicInfo* nics, int count)
{
    HMODULE lib = LoadLibraryW(L"wlanapi.dll");
    if (!lib) return;
    typedef DWORD (WINAPI *PFN_OPEN)(DWORD, PVOID, PDWORD, PHANDLE);
    typedef DWORD (WINAPI *PFN_ENUM)(HANDLE, PVOID, PWLAN_INTERFACE_INFO_LIST*);
    typedef DWORD (WINAPI *PFN_QUERY)(HANDLE, const GUID*, WLAN_INTF_OPCODE,
        PVOID, PDWORD, PVOID*, PWLAN_OPCODE_VALUE_TYPE);
    typedef VOID (WINAPI *PFN_FREE)(PVOID);
    typedef DWORD (WINAPI *PFN_CLOSE)(HANDLE, PVOID);

    PFN_OPEN open = (PFN_OPEN)GetProcAddress(lib, "WlanOpenHandle");
    PFN_ENUM enumerate = (PFN_ENUM)GetProcAddress(lib, "WlanEnumInterfaces");
    PFN_QUERY query = (PFN_QUERY)GetProcAddress(lib, "WlanQueryInterface");
    PFN_FREE freeMem = (PFN_FREE)GetProcAddress(lib, "WlanFreeMemory");
    PFN_CLOSE close = (PFN_CLOSE)GetProcAddress(lib, "WlanCloseHandle");
    if (!open || !enumerate || !query || !freeMem || !close) {
        FreeLibrary(lib);
        return;
    }

    HANDLE h = NULL;
    DWORD ver = 0;
    if (open(2, NULL, &ver, &h) != ERROR_SUCCESS) {
        FreeLibrary(lib);
        return;
    }

    PWLAN_INTERFACE_INFO_LIST list = NULL;
    if (enumerate(h, NULL, &list) == ERROR_SUCCESS && list) {
        for (DWORD i = 0; i < list->dwNumberOfItems; i++) {
            WLAN_INTERFACE_INFO* ii = &list->InterfaceInfo[i];
            if (ii->isState != wlan_interface_state_connected) continue;

            PVOID data = NULL;
            DWORD sz = 0;
            WLAN_OPCODE_VALUE_TYPE vt;
            if (query(h, &ii->InterfaceGuid, wlan_intf_opcode_current_connection,
                    NULL, &sz, &data, &vt) != ERROR_SUCCESS || !data)
                continue;

            WLAN_CONNECTION_ATTRIBUTES* ca = (WLAN_CONNECTION_ATTRIBUTES*)data;
            DWORD n = ca->wlanAssociationAttributes.dot11Ssid.uSSIDLength;
            if (n > 0 && n <= 32) {
                char ssid[33];
                memcpy(ssid, ca->wlanAssociationAttributes.dot11Ssid.ucSSID, n);
                ssid[n] = 0;
                wchar_t wssid[64] = { 0 };
                if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, ssid, -1,
                        wssid, 64) <= 0)
                    MultiByteToWideChar(CP_ACP, 0, ssid, -1, wssid, 64);
                wchar_t gs[64] = { 0 };
                StringFromGUID2(ii->InterfaceGuid, gs, 64);
                for (int k = 0; k < count; k++) {
                    if (nics[k].guid[0] && _wcsicmp(nics[k].guid, gs) == 0) {
                        wcsncpy_s(nics[k].ssid, 64, wssid, _TRUNCATE);
                        break;
                    }
                }
            }
            freeMem(data);
        }
        freeMem(list);
    }
    close(h, NULL);
    FreeLibrary(lib);
}

static void SortNics(NicInfo* nics, int n)
{
    for (int i = 1; i < n; i++) {
        NicInfo tmp = nics[i];
        int score = (tmp.connected ? 0 : 2) + (tmp.wireless ? 0 : 1);
        int j = i - 1;
        while (j >= 0) {
            int sj = (nics[j].connected ? 0 : 2) + (nics[j].wireless ? 0 : 1);
            if (sj <= score) break;
            nics[j + 1] = nics[j];
            j--;
        }
        nics[j + 1] = tmp;
    }
}

static int EnumNics(NicInfo* out, int cap)
{
    GUID guid = { 0x4d36e972, 0xe325, 0x11ce,
        { 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 } };
    HDEVINFO devs = SetupDiGetClassDevsW(&guid, NULL, NULL, DIGCF_PRESENT);
    if (devs == INVALID_HANDLE_VALUE) return 0;

    static const wchar_t* kVirtual[] = {
        L"Virtual", L"Miniport", L"Loopback", L"WAN", L"Tunnel", L"VPN",
        L"Bluetooth", L"Hyper-V", L"Wi-Fi Direct", L"TAP-", L"Kernel Debug"
    };
    static const wchar_t* kWireless[] = {
        L"Wi-Fi", L"WiFi", L"Wireless", L"WLAN", L"802.11"
    };

    int n = 0;
    SP_DEVINFO_DATA data;
    data.cbSize = sizeof(data);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(devs, i, &data); i++) {
        wchar_t id[512] = { 0 };
        if (!SetupDiGetDeviceInstanceIdW(devs, &data, id, 512, NULL)) continue;
        if (_wcsnicmp(id, L"PCI\\", 4) != 0 && _wcsnicmp(id, L"USB\\", 4) != 0)
            continue;

        wchar_t key[9] = { 0 };
        bool hasKey = ExtractPciKey(id, key);
        wchar_t name[192] = { 0 };
        if (hasKey) LookupIdTable(g_nicIds, g_nicIdCount, key, name, 192);
        if (!name[0]) {
            GetDeviceString(devs, &data, SPDRP_FRIENDLYNAME, name, 192);
            if (!name[0]) GetDeviceString(devs, &data, SPDRP_DEVICEDESC, name, 192);
        }
        if (!name[0]) continue;
        if (NameHasAny(name, kVirtual, 11)) continue;

        bool wireless = wcsstr(id, L"CC_0280") != NULL;
        if (!wireless) wireless = NameHasAny(name, kWireless, 5);

        bool dup = false;
        for (int j = 0; j < n; j++) {
            if (wcscmp(out[j].name, name) == 0) {
                dup = true;
                break;
            }
        }
        if (dup) continue;

        memset(&out[n], 0, sizeof(out[n]));
        wcsncpy_s(out[n].name, 192, name, _TRUNCATE);
        out[n].wireless = wireless;

        HKEY devKey = SetupDiOpenDevRegKey(devs, &data, DICS_FLAG_GLOBAL, 0,
            DIREG_DRV, KEY_READ);
        if (devKey != INVALID_HANDLE_VALUE) {
            DWORD type = 0, sz = sizeof(out[n].guid);
            if (RegQueryValueExW(devKey, L"NetCfgInstanceId", NULL, &type,
                    (LPBYTE)out[n].guid, &sz) == ERROR_SUCCESS)
                out[n].guid[63] = 0;
            RegCloseKey(devKey);
        }
        n++;
        if (n >= cap) break;
    }
    SetupDiDestroyDeviceInfoList(devs);

    FillNicNetwork(out, n);
    FillNicSsids(out, n);
    SortNics(out, n);
    return n;
}

// ===========================================================================
// Configuration and palette
// ===========================================================================

enum { THEME_PLAIN = 0, THEME_ETHEREAL = 1, THEME_SPLIT = 2 };
enum { THEME_COUNT = 3 };

struct Config
{
    int theme;
    int hueFollow;
    int hue;
    int opacity;
    int radius;
    int showCpu, showBoard, showMem, showDisk, showGpu, showNet;
    int lightMode;   // 0 auto, 1 light, 2 dark
    int posDx, posDy; // panel offset in dp
};

static Config g_cfg = { THEME_PLAIN, 1, 250, 88, 16, 1, 1, 1, 1, 1, 1, 0, 0, 0 };
static wchar_t g_iniPath[MAX_PATH] = { 0 };

static int ClampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

// Hue actually used: sampled from the wallpaper when "follow wallpaper" is on,
// otherwise the hand-picked value.
static int g_sampledHue = 250;
static int HueDeg(void) { return ClampInt(g_cfg.hueFollow ? g_sampledHue : g_cfg.hue, 0, 360); }

static bool EnsureDirectory(const wchar_t* dir)
{
    if (!dir || !dir[0]) return false;
    DWORD a = GetFileAttributesW(dir);
    if (a != INVALID_FILE_ATTRIBUTES) return (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
    return CreateDirectoryW(dir, NULL) != FALSE || GetLastError() == ERROR_ALREADY_EXISTS;
}

// Writable check that does not leave an empty file behind.
static bool CanWriteFile(const wchar_t* path)
{
    bool existed = GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    if (!existed) DeleteFileW(path);
    return true;
}

// Portable first: HSpec.ini next to the executable. Read-only media (PE on a
// CD or a locked USB stick) falls back to %APPDATA%\HSpec\HSpec.ini.
static void ResolveIniPath(void)
{
    if (g_iniPath[0]) return;

    wchar_t exe[MAX_PATH] = { 0 };
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t* slash = wcsrchr(exe, L'\\');
    if (slash) *slash = 0;

    wchar_t candidate[MAX_PATH];
    WPrintf(candidate, MAX_PATH, L"%s\\HSpec.ini", exe);
    if (GetFileAttributesW(candidate) != INVALID_FILE_ATTRIBUTES || CanWriteFile(candidate)) {
        wcsncpy_s(g_iniPath, MAX_PATH, candidate, _TRUNCATE);
        return;
    }

    wchar_t appdata[MAX_PATH] = { 0 };
    DWORD n = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        wchar_t dir[MAX_PATH];
        WPrintf(dir, MAX_PATH, L"%s\\HSpec", appdata);
        if (EnsureDirectory(dir)) {
            WPrintf(candidate, MAX_PATH, L"%s\\HSpec.ini", dir);
            wcsncpy_s(g_iniPath, MAX_PATH, candidate, _TRUNCATE);
        }
    }
}

static void LoadConfig(void)
{
    ResolveIniPath();
    if (!g_iniPath[0]) return;
    g_cfg.theme = (int)GetPrivateProfileIntW(L"panel", L"theme", g_cfg.theme, g_iniPath);
    // Migrate the old 4-theme numbering (0 plain, 1 light card, 2 ethereal,
    // 3 split) - light card is gone, so 1 and 2 both land on the card theme.
    if (g_cfg.theme == 3) g_cfg.theme = THEME_SPLIT;
    else if (g_cfg.theme == 2 || g_cfg.theme == 1) g_cfg.theme = THEME_ETHEREAL;
    else g_cfg.theme = THEME_PLAIN;
    g_cfg.theme = ClampInt(g_cfg.theme, 0, THEME_COUNT - 1);
    g_cfg.hueFollow = ClampInt((int)GetPrivateProfileIntW(L"panel", L"hueFollow", g_cfg.hueFollow, g_iniPath), 0, 1);
    g_cfg.hue = ClampInt((int)GetPrivateProfileIntW(L"panel", L"hue", g_cfg.hue, g_iniPath), 0, 360);
    g_cfg.opacity = ClampInt((int)GetPrivateProfileIntW(L"panel", L"opacity", g_cfg.opacity, g_iniPath), 40, 100);
    g_cfg.radius = ClampInt((int)GetPrivateProfileIntW(L"panel", L"radius", g_cfg.radius, g_iniPath), 0, 24);
    g_cfg.showCpu = ClampInt((int)GetPrivateProfileIntW(L"panel", L"cpu", g_cfg.showCpu, g_iniPath), 0, 1);
    g_cfg.showBoard = ClampInt((int)GetPrivateProfileIntW(L"panel", L"board", g_cfg.showBoard, g_iniPath), 0, 1);
    g_cfg.showMem = ClampInt((int)GetPrivateProfileIntW(L"panel", L"mem", g_cfg.showMem, g_iniPath), 0, 1);
    g_cfg.showDisk = ClampInt((int)GetPrivateProfileIntW(L"panel", L"disk", g_cfg.showDisk, g_iniPath), 0, 1);
    g_cfg.showGpu = ClampInt((int)GetPrivateProfileIntW(L"panel", L"gpu", g_cfg.showGpu, g_iniPath), 0, 1);
    g_cfg.showNet = ClampInt((int)GetPrivateProfileIntW(L"panel", L"net", g_cfg.showNet, g_iniPath), 0, 1);
    g_cfg.lightMode = ClampInt((int)GetPrivateProfileIntW(L"panel", L"lightMode", g_cfg.lightMode, g_iniPath), 0, 2);
    g_cfg.posDx = ClampInt((int)GetPrivateProfileIntW(L"panel", L"posDx", g_cfg.posDx, g_iniPath), -400, 400);
    g_cfg.posDy = ClampInt((int)GetPrivateProfileIntW(L"panel", L"posDy", g_cfg.posDy, g_iniPath), -400, 400);
}

static void SaveConfig(void)
{
    ResolveIniPath();
    if (!g_iniPath[0]) return;
    wchar_t buf[16];
    struct { const wchar_t* key; int value; } items[] = {
        { L"theme", g_cfg.theme },
        { L"hueFollow", g_cfg.hueFollow },
        { L"hue", g_cfg.hue },
        { L"opacity", g_cfg.opacity },
        { L"radius", g_cfg.radius },
        { L"cpu", g_cfg.showCpu },
        { L"board", g_cfg.showBoard },
        { L"mem", g_cfg.showMem },
        { L"disk", g_cfg.showDisk },
        { L"gpu", g_cfg.showGpu },
        { L"net", g_cfg.showNet },
        { L"lightMode", g_cfg.lightMode },
        { L"posDx", g_cfg.posDx },
        { L"posDy", g_cfg.posDy },
    };
    for (int i = 0; i < (int)(sizeof(items) / sizeof(items[0])); i++) {
        WPrintf(buf, 16, L"%d", items[i].value);
        WritePrivateProfileStringW(L"panel", items[i].key, buf, g_iniPath);
    }
}

// OKLCH -> sRGB. Same colour space the Halo theme uses, so the panel and the
// website can share one hue knob.
static Gdiplus::Color OklchColor(double L, double C, double hue, int alpha)
{
    double hr = hue * 3.14159265358979323846 / 180.0;
    double a = C * cos(hr);
    double b = C * sin(hr);

    double l_ = L + 0.3963377774 * a + 0.2158037573 * b;
    double m_ = L - 0.1055613458 * a - 0.0638541728 * b;
    double s_ = L - 0.0894841775 * a - 1.2914855480 * b;
    double l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;

    double rgb[3] = {
         4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
        -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
        -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s,
    };

    BYTE out[3];
    for (int i = 0; i < 3; i++) {
        double v = rgb[i];
        v = (v <= 0.0031308) ? 12.92 * v : 1.055 * pow(v, 1.0 / 2.4) - 0.055;
        if (v < 0.0) v = 0.0;
        if (v > 1.0) v = 1.0;
        out[i] = (BYTE)(v * 255.0 + 0.5);
    }
    return Gdiplus::Color((BYTE)alpha, out[0], out[1], out[2]);
}

// sRGB -> OKLCH chroma/hue. Used to pull one accent hue out of the wallpaper.
static void SrgbToOklchHue(int r, int g, int b, double* chroma, double* hue)
{
    double ch[3] = { r / 255.0, g / 255.0, b / 255.0 };
    for (int i = 0; i < 3; i++)
        ch[i] = (ch[i] <= 0.04045) ? ch[i] / 12.92 : pow((ch[i] + 0.055) / 1.055, 2.4);

    double l = 0.4122214708 * ch[0] + 0.5363325363 * ch[1] + 0.0514459929 * ch[2];
    double m = 0.2119034982 * ch[0] + 0.6806995451 * ch[1] + 0.1073969566 * ch[2];
    double s = 0.0883024619 * ch[0] + 0.2817188376 * ch[1] + 0.6299787005 * ch[2];
    l = cbrt(l); m = cbrt(m); s = cbrt(s);

    double A = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
    double B = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
    *chroma = sqrt(A * A + B * B);
    double deg = atan2(B, A) * 180.0 / 3.14159265358979323846;
    if (deg < 0) deg += 360.0;
    *hue = deg;
}

struct Palette
{
    Gdiplus::Color primary;   // accent bar, switch, selection ring
    Gdiplus::Color chipBg;    // icon chip background
    Gdiplus::Color chipFg;    // icon strokes
    Gdiplus::Color title;     // row title
    Gdiplus::Color detail;    // row detail lines
    Gdiplus::Color card;      // card fill (alpha = opacity)
    Gdiplus::Color page;      // settings window background
    Gdiplus::Color divider;
    Gdiplus::Color itemBg;    // selected sidebar item
    Gdiplus::Color itemFg;
    bool darkCard;
};

static Palette MakePalette(bool darkCard, int cardAlpha)
{
    double h = (double)HueDeg();
    Palette p;
    p.darkCard = darkCard;
    if (darkCard) {
        p.primary = OklchColor(0.75, 0.14, h, 255);
        p.chipBg = OklchColor(0.33, 0.035, h, 255);
        p.chipFg = OklchColor(0.75, 0.10, h, 255);
        p.title = OklchColor(0.88, 0.010, h, 255);
        p.detail = OklchColor(0.70, 0.015, h, 255);
        p.card = OklchColor(0.23, 0.015, h, (BYTE)cardAlpha);
        p.page = OklchColor(0.16, 0.014, h, 255);
        p.divider = Gdiplus::Color(36, 255, 255, 255);
        p.itemBg = OklchColor(0.33, 0.035, h, 255);
        p.itemFg = OklchColor(0.75, 0.10, h, 255);
    } else {
        p.primary = OklchColor(0.70, 0.14, h, 255);
        p.chipBg = OklchColor(0.95, 0.025, h, 255);
        p.chipFg = OklchColor(0.55, 0.12, h, 255);
        p.title = OklchColor(0.25, 0.020, h, 255);
        p.detail = OklchColor(0.42, 0.020, h, 255);
        p.card = OklchColor(1.0, 0.0, h, (BYTE)cardAlpha);
        p.page = OklchColor(0.95, 0.010, h, 255);
        p.divider = Gdiplus::Color(22, 0, 0, 0);
        p.itemBg = OklchColor(0.95, 0.025, h, 255);
        p.itemFg = OklchColor(0.55, 0.12, h, 255);
    }
    return p;
}

// ===========================================================================
// UI model
// ===========================================================================

enum IconKind { ICON_CPU = 0, ICON_BOARD, ICON_RAM, ICON_DISK, ICON_GPU, ICON_NET };

struct UiRow
{
    int icon;
    wchar_t title[256];
    wchar_t details[4][256];
    int detailCount;
};

static UiRow g_rows[24];
static int g_rowCount = 0;
static SmbiosInfo g_smbios;
static UiRow g_scratchRow;
static bool g_skipSection = false;

static UiRow* ModelAddRow(int icon, const wchar_t* title)
{
    // Sections switched off in the settings window build into a scratch row so
    // the collection code stays untouched and nothing lands in the model.
    if (g_skipSection) {
        memset(&g_scratchRow, 0, sizeof(g_scratchRow));
        return &g_scratchRow;
    }
    if (g_rowCount >= 24) return &g_rows[23];
    UiRow* r = &g_rows[g_rowCount++];
    memset(r, 0, sizeof(*r));
    r->icon = icon;
    wcsncpy_s(r->title, 256, title ? title : L"", _TRUNCATE);
    return r;
}

static void ModelAddDetail(UiRow* r, const wchar_t* text)
{
    if (!r || r->detailCount >= 4) return;
    wcsncpy_s(r->details[r->detailCount++], 256, text ? text : L"", _TRUNCATE);
}

// ---------------------------------------------------------------------------
// 目标系统信息：制造商 / 型号 / OEM Logo / 系统版本 / 用户目录
// 供「系统信息 + 资料保存」使用。全部来自注册表与 Shell API，无 WMI 依赖。
// ---------------------------------------------------------------------------

struct TargetInfo
{
    wchar_t manufacturer[128];
    wchar_t model[192];
    wchar_t phone[64];
    wchar_t supportUrl[256];
    wchar_t logoFile[512];      // OEM Logo 实际文件（不存在则为空）

    wchar_t systemDrive[8];     // "C:"
    wchar_t computerName[128];
    wchar_t osName[192];
    wchar_t userName[128];
    wchar_t edition[128];       // CoreCountrySpecific
    wchar_t arch[32];           // 64 位
    wchar_t kernel[32];         // 6.3
    wchar_t releaseId[32];      // 23H2
    wchar_t build[32];          // 22631
    wchar_t ubr[16];            // 6199
    wchar_t lab[160];           // 22631.ni_release.220506-1250
    wchar_t installDate[64];    // 2023/03/07 01:11:32
    wchar_t regUser[128];
    wchar_t regOrg[128];

    wchar_t desktop[512];
    wchar_t downloads[512];
    wchar_t favorites[512];
    wchar_t documents[512];
    wchar_t pictures[512];
    wchar_t music[512];
    wchar_t videos[512];
};

static bool FileExistsW(const wchar_t* path)
{
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static void RegReadStr(HKEY root, const wchar_t* sub, const wchar_t* name,
    wchar_t* out, int cch)
{
    if (!out || cch <= 0) return;
    out[0] = 0;
    HKEY k = NULL;
    if (RegOpenKeyExW(root, sub, 0, KEY_READ | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS)
        return;
    DWORD type = 0;
    DWORD size = (DWORD)cch * sizeof(wchar_t);
    if (RegQueryValueExW(k, name, NULL, &type, (LPBYTE)out, &size) != ERROR_SUCCESS)
        out[0] = 0;
    else
        out[cch - 1] = 0;
    RegCloseKey(k);
}

static DWORD RegReadDword(HKEY root, const wchar_t* sub, const wchar_t* name)
{
    HKEY k = NULL;
    if (RegOpenKeyExW(root, sub, 0, KEY_READ | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS)
        return 0;
    DWORD type = 0, val = 0, size = sizeof(val);
    if (RegQueryValueExW(k, name, NULL, &type, (LPBYTE)&val, &size) != ERROR_SUCCESS)
        val = 0;
    RegCloseKey(k);
    return val;
}

static void KnownFolderPath(const KNOWNFOLDERID& id, wchar_t* out, int cch)
{
    out[0] = 0;
    PWSTR p = NULL;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, NULL, &p)) && p) {
        wcsncpy_s(out, cch, p, _TRUNCATE);
        CoTaskMemFree(p);
    }
}

static void CollectTargetInfo(TargetInfo* t)
{
    memset(t, 0, sizeof(*t));

    SmbiosInfo si;
    memset(&si, 0, sizeof(si));
    ParseSmbios(&si);
    SmbiosRegFallback(&si);

    const wchar_t* kOem = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\OEMInformation";
    RegReadStr(HKEY_LOCAL_MACHINE, kOem, L"Manufacturer", t->manufacturer, 128);
    RegReadStr(HKEY_LOCAL_MACHINE, kOem, L"Model", t->model, 192);
    RegReadStr(HKEY_LOCAL_MACHINE, kOem, L"SupportPhone", t->phone, 64);
    RegReadStr(HKEY_LOCAL_MACHINE, kOem, L"SupportURL", t->supportUrl, 256);
    if (!t->manufacturer[0])
        wcsncpy_s(t->manufacturer, 128, si.sysManufacturer, _TRUNCATE);
    if (!t->model[0])
        wcsncpy_s(t->model, 192, si.sysProduct, _TRUNCATE);

    // OEM Logo：先读注册表，接受 %SystemRoot% 形式的路径；都没有就找 System32\oemlogo.bmp
    {
        wchar_t raw[512] = { 0 };
        RegReadStr(HKEY_LOCAL_MACHINE, kOem, L"Logo", raw, 512);
        if (raw[0]) {
            wchar_t expanded[512] = { 0 };
            ExpandEnvironmentStringsW(raw, expanded, 512);
            if (FileExistsW(expanded))
                wcsncpy_s(t->logoFile, 512, expanded, _TRUNCATE);
        }
        if (!t->logoFile[0]) {
            wchar_t sys[MAX_PATH] = { 0 };
            GetSystemDirectoryW(sys, MAX_PATH);
            wcscat_s(sys, MAX_PATH, L"\\oemlogo.bmp");
            if (FileExistsW(sys))
                wcsncpy_s(t->logoFile, 512, sys, _TRUNCATE);
        }
    }

    const wchar_t* kCv = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    RegReadStr(HKEY_LOCAL_MACHINE, kCv, L"ProductName", t->osName, 192);
    RegReadStr(HKEY_LOCAL_MACHINE, kCv, L"EditionID", t->edition, 128);
    RegReadStr(HKEY_LOCAL_MACHINE, kCv, L"CurrentVersion", t->kernel, 32);
    RegReadStr(HKEY_LOCAL_MACHINE, kCv, L"DisplayVersion", t->releaseId, 32);
    if (!t->releaseId[0])
        RegReadStr(HKEY_LOCAL_MACHINE, kCv, L"ReleaseId", t->releaseId, 32);
    RegReadStr(HKEY_LOCAL_MACHINE, kCv, L"CurrentBuildNumber", t->build, 32);
    RegReadStr(HKEY_LOCAL_MACHINE, kCv, L"BuildLabEx", t->lab, 160);
    RegReadStr(HKEY_LOCAL_MACHINE, kCv, L"RegisteredOwner", t->regUser, 128);
    RegReadStr(HKEY_LOCAL_MACHINE, kCv, L"RegisteredOrganization", t->regOrg, 128);

    DWORD ubr = RegReadDword(HKEY_LOCAL_MACHINE, kCv, L"UBR");
    if (ubr)
        WPrintf(t->ubr, 16, L"%lu", ubr);

    // 安装日期：InstallDate 是 Unix 秒
    {
        DWORD ts = RegReadDword(HKEY_LOCAL_MACHINE, kCv, L"InstallDate");
        if (ts) {
            __time64_t tt = (__time64_t)ts;
            struct tm lt;
            if (_localtime64_s(&lt, &tt) == 0)
                WPrintf(t->installDate, 64, L"%04d/%02d/%02d %02d:%02d:%02d",
                    lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
                    lt.tm_hour, lt.tm_min, lt.tm_sec);
        }
    }

    // 位数
    {
        SYSTEM_INFO sysInfo;
        GetNativeSystemInfo(&sysInfo);
        wcscpy_s(t->arch, 32,
            sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? L"64 位"
            : sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ? L"ARM64"
            : L"32 位");
    }

    // 系统盘 / 计算机名 / 用户名
    {
        wchar_t win[MAX_PATH] = { 0 };
        GetWindowsDirectoryW(win, MAX_PATH);
        wcsncpy_s(t->systemDrive, 8, win, 2);
        t->systemDrive[2] = 0;
    }
    {
        DWORD cch = 128;
        GetComputerNameW(t->computerName, &cch);
    }
    {
        DWORD cch = 128;
        GetUserNameW(t->userName, &cch);
    }

    KnownFolderPath(FOLDERID_Desktop, t->desktop, 512);
    KnownFolderPath(FOLDERID_Downloads, t->downloads, 512);
    KnownFolderPath(FOLDERID_Favorites, t->favorites, 512);
    KnownFolderPath(FOLDERID_Documents, t->documents, 512);
    KnownFolderPath(FOLDERID_Pictures, t->pictures, 512);
    KnownFolderPath(FOLDERID_Music, t->music, 512);
    KnownFolderPath(FOLDERID_Videos, t->videos, 512);
}

static void WriteUtf8(FILE* f, const wchar_t* text)
{
    if (!text || !text[0]) return;
    char buf[4096];
    int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, buf, (int)sizeof(buf), NULL, NULL);
    if (n > 1) fwrite(buf, 1, (size_t)(n - 1), f);
}

// 把目标信息写成一份可读的 txt（「保存所有信息」与 --dump-info 共用）
static bool SaveTargetInfo(const wchar_t* path, const TargetInfo* t)
{
    FILE* f = NULL;
    if (_wfopen_s(&f, path, L"wb") != 0 || !f) return false;
    fwrite("\xEF\xBB\xBF", 1, 3, f);   // UTF-8 BOM

    wchar_t line[1024];
    WriteUtf8(f, L"目标系统信息查看与资料保存\r\n");
    WriteUtf8(f, L"================================================\r\n\r\n");
    WriteUtf8(f, L"【厂商与支持】\r\n");
#define KV(label, field) do { \
        WPrintf(line, 1024, L"%s: %s\r\n", label, (t->field)[0] ? (t->field) : L""); \
        WriteUtf8(f, line); \
    } while (0)
    KV(L"制造商", manufacturer);
    KV(L"型号", model);
    KV(L"电话号码", phone);
    KV(L"网址", supportUrl);
    KV(L"Logo位置", logoFile);
    WriteUtf8(f, L"\r\n【系统】\r\n");
    KV(L"系统盘符", systemDrive);
    KV(L"计算机名", computerName);
    KV(L"系统名称", osName);
    KV(L"用户名称", userName);
    KV(L"版本类别", edition);
    KV(L"系统位数", arch);
    KV(L"版本内核", kernel);
    KV(L"发行号", releaseId);
    KV(L"版本编号", build);
    KV(L"补丁号", ubr);
    KV(L"实验版本", lab);
    KV(L"安装日期", installDate);
    KV(L"注册用户", regUser);
    KV(L"注册组织", regOrg);
    WriteUtf8(f, L"\r\n【用户目录】\r\n");
    KV(L"桌面路径", desktop);
    KV(L"下载路径", downloads);
    KV(L"收藏夹", favorites);
    KV(L"文档路径", documents);
    KV(L"图片路径", pictures);
    KV(L"音乐路径", music);
    KV(L"视频路径", videos);
#undef KV
    fclose(f);
    return true;
}

static void BuildModel(void)
{
    g_rowCount = 0;
    memset(g_rows, 0, sizeof(g_rows));
    memset(&g_smbios, 0, sizeof(g_smbios));
    ParseSmbios(&g_smbios);
    SmbiosRegFallback(&g_smbios);

    // ---- CPU ----
    g_skipSection = !g_cfg.showCpu;
    wchar_t cpuBrand[192] = { 0 };
    GetCpuBrand(cpuBrand, 192);
    if (!cpuBrand[0] && g_smbios.cpuVersion[0])
        wcsncpy_s(cpuBrand, 192, g_smbios.cpuVersion, _TRUNCATE);
    int cores = GetPhysicalCores();
    int threads = GetLogicalCpus();

    UiRow* r = ModelAddRow(ICON_CPU, cpuBrand[0] ? cpuBrand : L"处理器");
    if (cores > 0) {
        wchar_t d[128];
        if (threads > cores)
            WPrintf(d, 128, L"核心: %d    线程: %d", cores, threads);
        else
            WPrintf(d, 128, L"核心: %d", cores);
        ModelAddDetail(r, d);
    }

    // ---- Board / firmware ----
    // Title carries vendor + product ("主板: 联想 LNVNB161216"); boot mode and
    // the Secure Boot cert share one detail line so the section stays compact.
    g_skipSection = !g_cfg.showBoard;
    wchar_t boardMfr[128] = { 0 };
    if (g_smbios.boardManufacturer[0]) {
        wcsncpy_s(boardMfr, 128, g_smbios.boardManufacturer, _TRUNCATE);
        TranslateVendorText(boardMfr, 128);
    }
    const wchar_t* boardProduct = g_smbios.boardProduct[0] ? g_smbios.boardProduct
        : (g_smbios.sysProduct[0] ? g_smbios.sysProduct : L"");
    wchar_t boardTitle[256] = L"主板";
    if (boardMfr[0] && boardProduct[0])
        WPrintf(boardTitle, 256, L"主板: %s %s", boardMfr, boardProduct);
    else if (boardProduct[0])
        WPrintf(boardTitle, 256, L"主板: %s", boardProduct);
    else if (boardMfr[0])
        WPrintf(boardTitle, 256, L"主板: %s", boardMfr);
    r = ModelAddRow(ICON_BOARD, boardTitle);

    if (g_smbios.biosVersion[0]) {
        wchar_t d[256];
        WPrintf(d, 256, L"固件: %s", g_smbios.biosVersion);
        ModelAddDetail(r, d);
    }

    int fw = GetFirmwareKind();
    int sb = (fw == 2) ? GetSecureBootState() : -1;
    wchar_t boot[128];
    if (fw == 2) {
        if (sb == 1) wcscpy_s(boot, 128, L"UEFI (Secure Boot)");
        else if (sb == 0) wcscpy_s(boot, 128, L"UEFI (Secure Boot 关闭)");
        else wcscpy_s(boot, 128, L"UEFI");
    } else if (fw == 1) {
        wcscpy_s(boot, 128, L"Legacy BIOS");
    } else {
        wcscpy_s(boot, 128, L"未知");
    }
    {
        wchar_t cert[64] = { 0 };
        bool haveCert = (fw == 2 && sb == 1) && CollectBootCert(cert, 64);
        wchar_t d[256];
        if (haveCert)
            WPrintf(d, 256, L"启动: %s    证书: %s", boot, cert);
        else
            WPrintf(d, 256, L"启动: %s", boot);
        ModelAddDetail(r, d);
    }

    // ---- Memory ----
    g_skipSection = !g_cfg.showMem;
    MEMORYSTATUSEX ms;
    memset(&ms, 0, sizeof(ms));
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);

    unsigned long long smbTotal = 0;
    for (int i = 0; i < g_smbios.memCount; i++) smbTotal += g_smbios.mem[i].bytes;

    unsigned long long totalBytes = smbTotal;
    if (totalBytes == 0) {
        ULONGLONG installedKb = 0;
        if (GetPhysicallyInstalledSystemMemory(&installedKb) && installedKb > 0)
            totalBytes = installedKb * 1024ULL;
    }
    if (totalBytes == 0) totalBytes = ms.ullTotalPhys;

    wchar_t memTitle[256];
    const wchar_t* memType = L"";
    const wchar_t* firstPart = L"";
    int firstSpeed = 0;
    if (g_smbios.memCount > 0) {
        memType = MemTypeName(g_smbios.mem[0].type);
        if (!memType) memType = L"";
        firstPart = g_smbios.mem[0].part;
        firstSpeed = g_smbios.mem[0].speed;
    }
    double totalGb = (double)totalBytes / 1073741824.0;
    int memCount = g_smbios.memCount;
    if (memType[0] && memCount > 1)
        WPrintf(memTitle, 256, L"内存: %.0f GB %s ×%d", totalGb, memType, memCount);
    else if (memType[0])
        WPrintf(memTitle, 256, L"内存: %.0f GB %s", totalGb, memType);
    else if (memCount > 1)
        WPrintf(memTitle, 256, L"内存: %.0f GB ×%d", totalGb, memCount);
    else
        WPrintf(memTitle, 256, L"内存: %.0f GB", totalGb);

    r = ModelAddRow(ICON_RAM, memTitle);

    if (g_smbios.memCount > 0) {
        wchar_t m0[128] = { 0 };
        wcsncpy_s(m0, 128, g_smbios.mem[0].manufacturer, _TRUNCATE);
        TranslateVendorText(m0, 128);
        if (m0[0]) {
            wchar_t d[256];
            wchar_t m1[128] = { 0 };
            if (g_smbios.memCount > 1 && g_smbios.mem[1].manufacturer[0]) {
                wcsncpy_s(m1, 128, g_smbios.mem[1].manufacturer, _TRUNCATE);
                TranslateVendorText(m1, 128);
            }
            if (m1[0] && wcscmp(m1, m0) != 0)
                WPrintf(d, 256, L"品牌: %s / %s", m0, m1);
            else
                WPrintf(d, 256, L"品牌: %s", m0);
            ModelAddDetail(r, d);
        }
    }

    if (firstPart[0]) {
        wchar_t memDetail[256] = { 0 };
        wcsncpy_s(memDetail, 256, firstPart, _TRUNCATE);
        if (g_smbios.memCount > 1 && g_smbios.mem[1].part[0]
            && wcscmp(g_smbios.mem[1].part, firstPart) != 0) {
            wcscat_s(memDetail, 256, L" / ");
            wcsncat_s(memDetail, 256, g_smbios.mem[1].part, _TRUNCATE);
        }
        if (firstSpeed > 0) {
            wchar_t modSize[32] = { 0 };
            if (g_smbios.memCount > 0)
                FormatCapacity(g_smbios.mem[0].bytes, modSize, 32);
            wchar_t d[384];
            if (modSize[0])
                WPrintf(d, 384, L"型号: %s    %s    %d MHz", memDetail, modSize, firstSpeed);
            else
                WPrintf(d, 384, L"型号: %s    %d MHz", memDetail, firstSpeed);
            ModelAddDetail(r, d);
        } else {
            wchar_t modSize[32] = { 0 };
            if (g_smbios.memCount > 0)
                FormatCapacity(g_smbios.mem[0].bytes, modSize, 32);
            wchar_t d[384];
            if (modSize[0])
                WPrintf(d, 384, L"型号: %s    %s", memDetail, modSize);
            else
                WPrintf(d, 384, L"型号: %s", memDetail);
            ModelAddDetail(r, d);
        }
    }

    // ---- GPU / display (WinOSInfo layout) ----
    // Title line carries the display (model, size, mode, refresh); every
    // adapter is then listed as a numbered line with its VRAM.  No "另一块"
    // wording: the numbering is the structure.
    g_skipSection = !g_cfg.showGpu;
    GpuInfo gpus[4];
    int gc = EnumGpus(gpus, 4);
    if (gc > 0) {
        // Order: in-use GPU first (WinPE has no desktop -> discrete first,
        // which EnumGpus already sorts by).
        int order[4];
        int act = GpuActiveIndex(gpus, gc);
        if (act < 0) {
            act = 0;
            for (int i = 0; i < gc; i++)
                if (gpus[i].rank == 0) { act = i; break; }
        }
        order[0] = act;
        for (int i = 0, n = 1; i < gc; i++)
            if (i != act) order[n++] = i;

        wchar_t title[256];
        DisplayInfo di;
        if (GetDisplayInfo(&di) && di.name[0]) {
            wchar_t size[32] = { 0 };
            if (di.diagIn > 3.0) WPrintf(size, 32, L"%.1f\"", di.diagIn);
            wchar_t mode[80] = { 0 };
            if (di.w > 0 && di.hz > 0) WPrintf(mode, 80, L"%dx%d %dHz", di.w, di.h, di.hz);
            else if (di.w > 0) WPrintf(mode, 80, L"%dx%d", di.w, di.h);
            if (size[0] && mode[0])
                WPrintf(title, 256, L"显卡: %s %s %s", di.name, size, mode);
            else if (mode[0])
                WPrintf(title, 256, L"显卡: %s %s", di.name, mode);
            else
                WPrintf(title, 256, L"显卡: %s", di.name);
        } else {
            WPrintf(title, 256, L"显卡: %s", gpus[act].name);
        }
        r = ModelAddRow(ICON_GPU, title);
        for (int i = 0; i < gc; i++) {
            wchar_t vram[32];
            FormatVram(GpuVramBytes(gpus[order[i]].key), vram, 32);
            wchar_t d[320];
            if (vram[0])
                WPrintf(d, 320, L"%d. %s %s", i + 1, gpus[order[i]].name, vram);
            else
                WPrintf(d, 320, L"%d. %s", i + 1, gpus[order[i]].name);
            ModelAddDetail(r, d);
        }
    }

    // ---- Disks ----
    g_skipSection = !g_cfg.showDisk;
    DiskInfo disks[8];
    memset(disks, 0, sizeof(disks));
    int dc = EnumDisks(disks, 6);
    if (dc == 0) {
        r = ModelAddRow(ICON_DISK, L"硬盘: 未检测到");
        (void)r;
    } else {
        for (int i = 0; i < dc; i++) {
            wchar_t title[200];
            WPrintf(title, 200, L"硬盘: %s", disks[i].model);
            r = ModelAddRow(ICON_DISK, title);
            wchar_t capText[32];
            FormatCapacity(disks[i].bytes, capText, 32);
            wchar_t d[256];
            WPrintf(d, 256, L"容量: %s    格式: %s    类型: %s",
                capText, disks[i].style, disks[i].bus);
            ModelAddDetail(r, d);
        }
    }

    // ---- Network ----
    g_skipSection = !g_cfg.showNet;
    NicInfo nics[8];
    int nc = EnumNics(nics, 8);
    for (int i = 0; i < nc; i++) {
        NicInfo& nic = nics[i];
        wchar_t title[256];
        WPrintf(title, 256, L"网卡: %s", nic.name);
        r = ModelAddRow(ICON_NET, title);

        wchar_t line[384] = { 0 };
        WPrintf(line, 384, L"类型: %s", nic.wireless ? L"无线" : L"有线");
        if (nic.mac[0]) {
            wchar_t tmp[384];
            WPrintf(tmp, 384, L"%s    MAC: %s", line, nic.mac);
            wcsncpy_s(line, 384, tmp, _TRUNCATE);
        }
        ModelAddDetail(r, line);

        wchar_t line2[384] = { 0 };
        if (nic.ip[0])
            WPrintf(line2, 384, L"IP: %s", nic.ip);
        if (nic.speedBps > 0) {
            wchar_t sp[32] = { 0 };
            FormatSpeed(nic.speedBps, sp, 32);
            if (sp[0]) {
                if (line2[0]) wcscat_s(line2, 384, L"    ");
                wcscat_s(line2, 384, L"速率: ");
                wcscat_s(line2, 384, sp);
            }
        }
        if (line2[0]) ModelAddDetail(r, line2);

        wchar_t line3[384] = { 0 };
        if (nic.ssid[0])
            WPrintf(line3, 384, L"SSID: %s", nic.ssid);
        if (nic.gateway[0]) {
            if (line3[0]) wcscat_s(line3, 384, L"    ");
            wcscat_s(line3, 384, L"网关: ");
            wcscat_s(line3, 384, nic.gateway);
        }
        if (line3[0]) ModelAddDetail(r, line3);
    }

    g_skipSection = false;
}

// ===========================================================================
// Rendering
// ===========================================================================

static ULONG_PTR g_gdiToken = 0;
static HWND g_hwnd = NULL;
static UINT g_dpi = 96;
// Effective dp->px scale for the panel: the monitor DPI ratio, possibly
// reduced by ComputeLayout so the panel always fits its monitor.
static double g_scale = 1.0;
static int g_winW = 420;
static int g_winH = 320;
// ClassIsland-style hover behaviour: pointing at the panel fades it out of
// the way, moving off fades it back in.  g_hoverAlpha is what is on screen,
// g_hoverTarget is where the fade is heading; the last pushed DIB is kept so
// the fade only re-blends instead of re-rendering text.
static int g_hoverAlpha = 255, g_hoverTarget = 255;
static HBITMAP g_pushBmp = NULL;
// Time-driven fade: remember where the fade started so each tick interpolates
// from real elapsed time instead of a fixed step (a fixed step stutters badly
// whenever a WM_TIMER is late or coalesced).
static ULONGLONG g_fadeStartTick = 0;
static int g_fadeStartAlpha = 255;
static const ULONGLONG kFadeMs = 150;
static bool g_fontReady = false;
static HANDLE g_fontReg = NULL;
// GDI+ cannot see fonts registered with AddFontMemResourceEx, so the same
// bytes also go into a private collection.  Needed by DrawTextGpLine.
static Gdiplus::PrivateFontCollection* g_gdipFonts = NULL;
static ULONGLONG g_wallpaperStamp = 0;
static NOTIFYICONDATAW g_nid;
static bool g_trayAdded = false;
static UINT g_taskbarCreatedMsg = 0;
static wchar_t g_dumpPath[MAX_PATH] = { 0 };
static wchar_t g_settingsDumpPath[MAX_PATH] = { 0 };
static wchar_t g_infoDumpPath[MAX_PATH] = { 0 };
static int g_dumpSetMode = 0;
static int g_settingsDumpPage = 0;
static bool g_openSettingsAtStart = false;
static int g_themeOverride = -1;

static const Gdiplus::Color kTitleColorDark(255, 26, 26, 26);
static const Gdiplus::Color kDetailColorDark(255, 58, 58, 58);
static const Gdiplus::Color kIconColorDark(255, 38, 38, 38);
static const Gdiplus::Color kTitleColorLight(255, 245, 245, 245);
static const Gdiplus::Color kDetailColorLight(255, 205, 205, 205);
static const Gdiplus::Color kIconColorLight(255, 235, 235, 235);

static bool g_lightText = false;
static Gdiplus::Color g_titleColor = kTitleColorDark;
static Gdiplus::Color g_detailColor = kDetailColorDark;
static Gdiplus::Color g_iconColor = kIconColorDark;

static void ApplyPalette(void)
{
    if (g_lightText) {
        g_titleColor = kTitleColorLight;
        g_detailColor = kDetailColorLight;
        g_iconColor = kIconColorLight;
    } else {
        g_titleColor = kTitleColorDark;
        g_detailColor = kDetailColorDark;
        g_iconColor = kIconColorDark;
    }
}

static bool GetWallpaperPath(wchar_t* out, int cap)
{
    out[0] = 0;
    // SPI_GETDESKWALLPAPER is the image currently in use (also for slideshows).
    if (SystemParametersInfoW(SPI_GETDESKWALLPAPER, (UINT)cap, out, 0) && out[0]) {
        if (GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES) return true;
    }
    wchar_t appdata[MAX_PATH] = { 0 };
    DWORD n = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        WPrintf(out, (size_t)cap, L"%s\\Microsoft\\Windows\\Themes\\TranscodedWallpaper",
            appdata);
        if (GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES) return true;
    }
    RegStrW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"WallPaper", out, (DWORD)cap);
    return out[0] != 0;
}

static ULONGLONG HashStamp(const wchar_t* path, ULONGLONG h)
{
    if (!path || !path[0]) return h;
    for (const wchar_t* c = path; *c; c++)
        h = (h ^ (ULONGLONG)(unsigned)*c) * 1099511628211ULL;
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fad)) return h;
    h = (h ^ (ULONGLONG)fad.ftLastWriteTime.dwLowDateTime) * 1099511628211ULL;
    h = (h ^ (ULONGLONG)fad.ftLastWriteTime.dwHighDateTime) * 1099511628211ULL;
    h = (h ^ (ULONGLONG)fad.nFileSizeLow) * 1099511628211ULL;
    h = (h ^ (ULONGLONG)fad.nFileSizeHigh) * 1099511628211ULL;
    return h;
}

// Changes whenever the active wallpaper image or solid background color does.
static ULONGLONG WallpaperStamp(void)
{
    ULONGLONG h = 1469598103934665603ULL;
    wchar_t path[MAX_PATH] = { 0 };
    if (GetWallpaperPath(path, MAX_PATH)) h = HashStamp(path, h);
    wchar_t bg[64] = { 0 };
    if (RegStrW(HKEY_CURRENT_USER, L"Control Panel\\Colors", L"Background", bg, 64)) {
        for (const wchar_t* c = bg; *c; c++)
            h = (h ^ (ULONGLONG)(unsigned)*c) * 1099511628211ULL;
    }
    return h;
}

// Screen position of the panel. PositionWindow, UpdateTheme and the card
// backdrop all need the same numbers, so keep them here.
static int PanelMarginX(void) { return (int)(18.0 * g_scale + 0.5); }
static int PanelMarginY(void) { return (int)(22.0 * g_scale + 0.5); }

static void RefreshWallpaperCache(void);

// Pick dark or light text from the wallpaper brightness behind the panel.
// Dark-card flag for the settings window and the card themes.  lightMode:
// 0 follow the system apps theme (wallpaper luminance as the fallback),
// 1 force light, 2 force dark.  The plain panel keeps its own wallpaper
// contrast so its text stays readable over any background.
static bool EffectiveDark(void)
{
    if (g_cfg.lightMode == 1) return false;
    if (g_cfg.lightMode == 2) return true;
    DWORD v = 1;
    if (RegDwordW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"AppsUseLightTheme", &v))
        return v == 0;
    return g_lightText;
}

// Wallpaper sampling (full decode + 100 GetPixel) is expensive; cache it on
// (wallpaper stamp, panel geometry).  Slider drags re-render every tick and
// must never land in here.
static ULONGLONG g_themeStamp = 0;
static int g_themeW = 0, g_themeH = 0;

static void UpdateTheme(void)
{
    wchar_t path[MAX_PATH] = { 0 };
    bool haveImage = false;

    ULONGLONG stamp = WallpaperStamp();
    if (stamp == g_themeStamp && g_winW == g_themeW && g_winH == g_themeH) {
        ApplyPalette();
        RefreshWallpaperCache();
        return;
    }

    if (GetWallpaperPath(path, MAX_PATH)) {
        Gdiplus::Bitmap img(path);
        if (img.GetLastStatus() == Gdiplus::Ok) {
            int iw = (int)img.GetWidth();
            int ih = (int)img.GetHeight();
            if (iw > 0 && ih > 0) {
                POINT origin = { 0, 0 };
                MONITORINFO mi;
                memset(&mi, 0, sizeof(mi));
                mi.cbSize = sizeof(mi);
                GetMonitorInfoW(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), &mi);

                int x = mi.rcMonitor.right - g_winW - PanelMarginX();
                int y = mi.rcMonitor.top + PanelMarginY();
                int mw = mi.rcMonitor.right - mi.rcMonitor.left;
                int mh = mi.rcMonitor.bottom - mi.rcMonitor.top;
                if (mw > 0 && mh > 0) {
                    double sx = (double)iw / mw;
                    double sy = (double)ih / mh;
                    double sum = 0;
                    int cnt = 0;
                    // Circular mean of the saturated samples gives the accent
                    // hue; grey-ish samples would only dilute it.
                    double hueX = 0, hueY = 0, hueW = 0;
                    for (int gy = 0; gy < 10; gy++) {
                        for (int gx = 0; gx < 10; gx++) {
                            int px = (int)((x + (gx + 0.5) * g_winW / 10.0) * sx);
                            int py = (int)((y + (gy + 0.5) * g_winH / 10.0) * sy);
                            if (px < 0 || py < 0 || px >= iw || py >= ih) continue;
                            Gdiplus::Color c;
                            img.GetPixel(px, py, &c);
                            sum += (c.GetR() * 299 + c.GetG() * 587 + c.GetB() * 114) / 1000.0;
                            cnt++;
                            double ch = 0, hd = 0;
                            SrgbToOklchHue(c.GetR(), c.GetG(), c.GetB(), &ch, &hd);
                            if (ch > 0.045) {
                                double rad = hd * 3.14159265358979323846 / 180.0;
                                hueX += cos(rad) * ch;
                                hueY += sin(rad) * ch;
                                hueW += ch;
                            }
                        }
                    }
                    if (cnt > 0) {
                        g_lightText = (sum / cnt) < 118.0;
                        if (hueW > 0.25) {
                            double deg = atan2(hueY, hueX) * 180.0 / 3.14159265358979323846;
                            if (deg < 0) deg += 360.0;
                            g_sampledHue = (int)(deg + 0.5) % 360;
                        }
                        ApplyPalette();
                        haveImage = true;
                    }
                }
            }
        }
    }

    g_themeStamp = stamp;
    g_themeW = g_winW;
    g_themeH = g_winH;

    if (!haveImage) {
        // Solid-color desktop: HKCU\Control Panel\Colors\Background = "R G B".
        wchar_t bg[64] = { 0 };
        if (RegStrW(HKEY_CURRENT_USER, L"Control Panel\\Colors", L"Background", bg, 64)) {
            int r = 0, g = 0, b = 0;
            if (swscanf_s(bg, L"%d %d %d", &r, &g, &b) == 3) {
                double avg = r * 0.299 + g * 0.587 + b * 0.114;
                g_lightText = avg < 118.0;
                ApplyPalette();
            }
        }
    }

    RefreshWallpaperCache();
}

static void LoadEmbeddedFont(HINSTANCE inst)
{
    HRSRC hr = FindResourceW(inst, MAKEINTRESOURCEW(IDR_FONT), MAKEINTRESOURCEW(10));
    if (!hr) return;
    HGLOBAL hg = LoadResource(inst, hr);
    if (!hg) return;
    void* data = LockResource(hg);
    DWORD sz = SizeofResource(inst, hr);
    if (!data || sz == 0) return;

    DWORD n = 0;
    HANDLE reg = AddFontMemResourceEx(data, sz, NULL, &n);
    if (!reg || n == 0) return;
    g_fontReg = reg;
    g_fontReady = true;

    // Same bytes for GDI+ (private collection).  GDI+ needs it to draw the
    // embedded face without grid fitting - see DrawTextGpLine.
    if (!g_gdipFonts) {
        g_gdipFonts = new Gdiplus::PrivateFontCollection();
        if (g_gdipFonts->GetLastStatus() != Gdiplus::Ok) {
            delete g_gdipFonts;
            g_gdipFonts = NULL;
        }
    }
    if (g_gdipFonts) g_gdipFonts->AddMemoryFont(data, (INT)sz);
}

static UINT GetDpiSafe(HWND hwnd)
{
    typedef UINT (WINAPI *PFN_GetDpiForWindow)(HWND);
    HMODULE u = GetModuleHandleW(L"user32.dll");
    if (u) {
        PFN_GetDpiForWindow fn = (PFN_GetDpiForWindow)GetProcAddress(u, "GetDpiForWindow");
        if (fn) {
            UINT d = fn(hwnd);
            if (d > 0) return d;
        }
    }
    HDC dc = GetDC(hwnd);
    UINT d = (UINT)GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(hwnd, dc);
    return d ? d : 96;
}

static int g_titleH = 24, g_detailH = 21, g_rowGap = 9, g_iconSize = 24, g_gapX = 10, g_padTop = 4;
// Card geometry (only meaningful when g_cfg.theme != THEME_PLAIN). g_cardMargin
// leaves room for the soft shadow, the window size stays identical.
static int g_cardMargin = 10, g_padX = 18, g_padY = 15, g_chipSize = 30;

static bool CardMode(void) { return g_cfg.theme != THEME_PLAIN; }
// Both remaining card themes (卡片 / 分离卡片) share the translucent
// blurred-wallpaper background.
static bool CardBlur(void) { return g_cfg.theme != THEME_PLAIN; }

static void ComputeLayout(void)
{
    double s = (double)g_dpi / 96.0;

    // Two passes at most: lay out at the monitor's DPI scale, then shrink
    // proportionally if the result would not fit the monitor.  Everything is
    // derived from s, so the panel scales as one piece.
    for (int pass = 0; pass < 2; pass++) {
        g_iconSize = (int)(24.0 * s + 0.5);
        g_gapX = (int)(10.0 * s + 0.5);
        g_titleH = (int)(26.0 * s + 0.5);
        g_detailH = (int)(22.0 * s + 0.5);
        g_rowGap = (int)(9.0 * s + 0.5);
        g_padTop = (int)(4.0 * s + 0.5);
        g_cardMargin = (int)(8.0 * s + 0.5);
        g_padX = (int)(14.0 * s + 0.5);
        g_padY = (int)(15.0 * s + 0.5);
        g_chipSize = (int)(30.0 * s + 0.5);
        // A card spends padding on both sides, so the window widens just
        // enough to keep the text column identical to the plain theme (no
        // new truncation).
        g_winW = (int)((CardMode() ? 526.0 : 480.0) * s + 0.5);

        int h;
        if (g_cfg.theme == THEME_SPLIT) {
            // One card per section: each row carries its own padY top+bottom,
            // cards are separated by rowGap inside a cardMargin frame.
            h = g_cardMargin * 2;
            for (int i = 0; i < g_rowCount; i++) {
                h += g_padY * 2 + g_titleH + g_rows[i].detailCount * g_detailH;
                if (i + 1 < g_rowCount) h += g_rowGap;
            }
        } else {
            h = CardMode() ? (g_cardMargin * 2 + g_padY * 2) : g_padTop;
            for (int i = 0; i < g_rowCount; i++)
                h += g_titleH + g_rows[i].detailCount * g_detailH + g_rowGap;
            if (!CardMode()) h += (int)(4.0 * s + 0.5);
        }
        g_winH = h;

        if (pass == 0) {
            POINT origin = { 0, 0 };
            HMONITOR mon = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
            MONITORINFO mi;
            memset(&mi, 0, sizeof(mi));
            mi.cbSize = sizeof(mi);
            if (GetMonitorInfoW(mon, &mi)) {
                double availW = (double)(mi.rcWork.right - mi.rcWork.left)
                    - 2.0 * 18.0 * s;
                double availH = (double)(mi.rcWork.bottom - mi.rcWork.top)
                    - 2.0 * 22.0 * s;
                double f = 1.0;
                if (availW > 1.0 && g_winW > availW) f = availW / (double)g_winW;
                if (availH > 1.0 && (double)g_winH * f > availH)
                    f = availH / (double)g_winH;
                if (f < 0.999) {
                    s *= f;
                    continue;   // relayout at the reduced scale
                }
            }
        }
        break;
    }
    if (s < 0.5) s = 0.5;
    g_scale = s;
}

static void AddRoundRect(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rc, float r)
{
    if (r <= 0) { path.AddRectangle(rc); return; }
    float d = r * 2;
    path.AddArc(rc.X, rc.Y, d, d, 180, 90);
    path.AddArc(rc.GetRight() - d, rc.Y, d, d, 270, 90);
    path.AddArc(rc.GetRight() - d, rc.GetBottom() - d, d, d, 0, 90);
    path.AddArc(rc.X, rc.GetBottom() - d, d, d, 90, 90);
    path.CloseFigure();
}

// ---------------------------------------------------------------------------
// Card layer (THEME_ETHEREAL / THEME_SPLIT)
// ---------------------------------------------------------------------------
//
// GDI+ has no backdrop blur. The panel sits on the desktop layer, so its
// backdrop *is* the wallpaper: keep one downscaled copy of it around and blur
// the card area with a down/up scale, only when the wallpaper changes.

static Gdiplus::Bitmap* g_wallCache = NULL;
static int g_wallCacheW = 0, g_wallCacheH = 0;

static ULONGLONG g_wallStamp = 0;

static void RefreshWallpaperCache(void)
{
    if (!CardBlur()) {
        // A flat card never reads the wallpaper.
        if (g_wallCache) {
            delete g_wallCache;
            g_wallCache = NULL;
        }
        g_wallCacheW = g_wallCacheH = 0;
        return;
    }

    // Already decoded for this wallpaper? Keep it - decoding twice per
    // settings click is what made every interaction feel stuck.
    ULONGLONG stamp = WallpaperStamp();
    if (g_wallCache && stamp == g_wallStamp) return;

    if (g_wallCache) {
        delete g_wallCache;
        g_wallCache = NULL;
    }
    g_wallCacheW = g_wallCacheH = 0;

    wchar_t path[MAX_PATH] = { 0 };
    if (!GetWallpaperPath(path, MAX_PATH)) return;

    Gdiplus::Bitmap src(path);
    if (src.GetLastStatus() != Gdiplus::Ok) return;
    int iw = (int)src.GetWidth();
    int ih = (int)src.GetHeight();
    if (iw <= 0 || ih <= 0) return;

    int dw = iw > 1280 ? 1280 : iw;
    int dh = ih * dw / iw;
    if (dh < 1) dh = 1;

    Gdiplus::Bitmap* dst = new Gdiplus::Bitmap(dw, dh, PixelFormat32bppPARGB);
    if (!dst) return;
    if (dst->GetLastStatus() != Gdiplus::Ok) {
        delete dst;
        return;
    }
    {
        Gdiplus::Graphics g(dst);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        g.DrawImage(&src, 0, 0, dw, dh);
    }
    g_wallCache = dst;
    g_wallCacheW = dw;
    g_wallCacheH = dh;
    g_wallStamp = stamp;
}

// Cheap soft shadow: expanding low-alpha outlines. A real blur would need a
// second pass over a padded bitmap for no visible gain at this size.
static void DrawPanelShadow(Gdiplus::Graphics& g, const Gdiplus::RectF& rc, float radius)
{
    for (int i = 7; i >= 1; i--) {
        float pad = (float)i * 0.85f;
        Gdiplus::GraphicsPath p;
        AddRoundRect(p, Gdiplus::RectF(rc.X - pad, rc.Y - pad + 2.0f,
            rc.Width + pad * 2, rc.Height + pad * 2), radius + pad);
        Gdiplus::Pen pen(Gdiplus::Color(10, 0, 0, 0), 1.8f);
        g.DrawPath(&pen, &p);
    }
}

static void DrawPanelCard(Gdiplus::Graphics& g, const Gdiplus::RectF& rc,
    float radius, const Palette& pal)
{
    Gdiplus::GraphicsPath path;
    AddRoundRect(path, rc, radius);

    // Every layer is painted through FillPath so the rounded edge gets the
    // SmoothingModeAntiAlias coverage ramp.  Do NOT clip: a GDI+ region clip
    // is hard-edged (no partial alpha), so a clipped DrawImage of the
    // wallpaper blur would show stair steps along the corner arcs, bleeding
    // through the semi-transparent card tint (opacity < 100).
    bool filled = false;
    if (CardBlur() && g_wallCache && g_wallCacheW > 0) {
        POINT origin = { 0, 0 };
        MONITORINFO mi;
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), &mi);
        int mw = mi.rcMonitor.right - mi.rcMonitor.left;
        int mh = mi.rcMonitor.bottom - mi.rcMonitor.top;
        if (mw > 0 && mh > 0) {
            float sx = (float)g_wallCacheW / (float)mw;
            float sy = (float)g_wallCacheH / (float)mh;
            int wx = mi.rcMonitor.right - g_winW - PanelMarginX();
            int wy = mi.rcMonitor.top + PanelMarginY();
            Gdiplus::RectF src((rc.X + wx) * sx, (rc.Y + wy) * sy,
                rc.Width * sx, rc.Height * sy);
            int bw = (int)(rc.Width / 7.0f);
            int bh = (int)(rc.Height / 7.0f);
            if (bw >= 6 && bh >= 6) {
                // NOTE: do not name this "small" - the Windows SDK has
                // "#define small char" (MinGW headers don't), which breaks
                // the MSVC build in the CI.
                Gdiplus::Bitmap smallBmp(bw, bh, PixelFormat32bppPARGB);
                if (smallBmp.GetLastStatus() == Gdiplus::Ok) {
                    Gdiplus::Graphics sg(&smallBmp);
                    sg.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                    sg.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
                    sg.DrawImage(g_wallCache,
                        Gdiplus::RectF(0, 0, (float)bw, (float)bh),
                        src.X, src.Y, src.Width, src.Height, Gdiplus::UnitPixel);

                    // Stretch the downsampled patch over rc through the
                    // rounded path: brush pixels map (0,0)-(bw,bh) onto rc.
                    Gdiplus::TextureBrush tex(&smallBmp, Gdiplus::WrapModeClamp);
                    Gdiplus::Matrix m;
                    m.Scale(rc.Width / (float)bw, rc.Height / (float)bh,
                        Gdiplus::MatrixOrderAppend);
                    m.Translate(rc.X, rc.Y, Gdiplus::MatrixOrderAppend);
                    tex.SetTransform(&m);
                    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
                    g.FillPath(&tex, &path);
                    filled = true;
                }
            }
        }
    }
    if (!filled) {
        Gdiplus::SolidBrush base(Gdiplus::Color(255,
            pal.page.GetR(), pal.page.GetG(), pal.page.GetB()));
        g.FillPath(&base, &path);
    }

    Gdiplus::SolidBrush fill(pal.card);
    g.FillPath(&fill, &path);
}

static void DrawGlyph(Gdiplus::Graphics& g, Gdiplus::Pen& pen, int kind,
    float x, float y, float sz, float s)
{
    const float u = sz / 24.0f;
    switch (kind) {
    case ICON_CPU: {
        // Chip with pins on all four sides, 14x14 body inside the 24 box.
        Gdiplus::GraphicsPath p;
        AddRoundRect(p, Gdiplus::RectF(x + 5 * u, y + 5 * u, 14 * u, 14 * u), 2 * u);
        g.DrawPath(&pen, &p);
        g.DrawRectangle(&pen, x + 9.5f * u, y + 9.5f * u, 5 * u, 5 * u);
        for (int i = 0; i < 3; i++) {
            float t = x + (8.0f + i * 4.0f) * u;
            g.DrawLine(&pen, t, y + 2 * u, t, y + 5 * u);
            g.DrawLine(&pen, t, y + 19 * u, t, y + 22 * u);
            float v = y + (8.0f + i * 4.0f) * u;
            g.DrawLine(&pen, x + 2 * u, v, x + 5 * u, v);
            g.DrawLine(&pen, x + 19 * u, v, x + 22 * u, v);
        }
        break;
    }
    case ICON_BOARD: {
        // Motherboard: wide board, CPU socket, two slots, front-panel header.
        Gdiplus::GraphicsPath p;
        AddRoundRect(p, Gdiplus::RectF(x + 2.5f * u, y + 4 * u, 19 * u, 16 * u), 2 * u);
        g.DrawPath(&pen, &p);
        g.DrawRectangle(&pen, x + 6 * u, y + 8 * u, 6 * u, 5 * u);
        g.DrawLine(&pen, x + 15 * u, y + 8 * u, x + 15 * u, y + 16 * u);
        g.DrawLine(&pen, x + 18 * u, y + 8 * u, x + 18 * u, y + 16 * u);
        g.DrawLine(&pen, x + 6 * u, y + 16.5f * u, x + 12 * u, y + 16.5f * u);
        break;
    }
    case ICON_RAM: {
        // RAM stick: wide body with notches and contact pins.
        Gdiplus::GraphicsPath p;
        AddRoundRect(p, Gdiplus::RectF(x + 2 * u, y + 7 * u, 20 * u, 10 * u), 1.5f * u);
        g.DrawPath(&pen, &p);
        g.DrawLine(&pen, x + 8.5f * u, y + 12 * u, x + 8.5f * u, y + 17 * u);
        g.DrawLine(&pen, x + 14.5f * u, y + 12 * u, x + 14.5f * u, y + 17 * u);
        for (int i = 0; i < 5; i++) {
            float t = x + (4.0f + i * 4.0f) * u;
            g.DrawLine(&pen, t, y + 17 * u, t, y + 19 * u);
        }
        break;
    }
    case ICON_DISK: {
        // Drive: rounded body, spindle and two slot lines.
        Gdiplus::GraphicsPath p;
        AddRoundRect(p, Gdiplus::RectF(x + 2 * u, y + 5 * u, 20 * u, 14 * u), 2 * u);
        g.DrawPath(&pen, &p);
        g.DrawEllipse(&pen, x + 6 * u, y + 9 * u, 6 * u, 6 * u);
        g.DrawLine(&pen, x + 15 * u, y + 9 * u, x + 19 * u, y + 9 * u);
        g.DrawLine(&pen, x + 15 * u, y + 13 * u, x + 19 * u, y + 13 * u);
        break;
    }
    case ICON_NET: {
        // Network: globe with two meridians.
        g.DrawEllipse(&pen, x + 3 * u, y + 3 * u, 18 * u, 18 * u);
        g.DrawLine(&pen, x + 3 * u, y + 12 * u, x + 21 * u, y + 12 * u);
        g.DrawEllipse(&pen, x + 8 * u, y + 3 * u, 8 * u, 18 * u);
        break;
    }
    case ICON_GPU: {
        // Display: monitor with stand.
        Gdiplus::GraphicsPath p;
        AddRoundRect(p, Gdiplus::RectF(x + 2 * u, y + 4 * u, 20 * u, 13 * u), 2 * u);
        g.DrawPath(&pen, &p);
        g.DrawLine(&pen, x + 12 * u, y + 17 * u, x + 12 * u, y + 20 * u);
        g.DrawLine(&pen, x + 8 * u, y + 20 * u, x + 16 * u, y + 20 * u);
        break;
    }
    default: break;
    }
}

// True when the font can draw every character in the line.  Used to fall
// back to a system CJK font for dynamic strings outside the embedded subset.
static bool FontCoversText(HDC dc, HFONT hf, const wchar_t* text)
{
    HGDIOBJ old = SelectObject(dc, hf);
    bool ok = true;
    WORD glyphs[128];
    const wchar_t* p = text;
    while (*p) {
        int n = (int)wcslen(p);
        if (n > 128) n = 128;
        if (GetGlyphIndicesW(dc, p, n, glyphs, GGI_MARK_NONEXISTING_GLYPHS) == GDI_ERROR) {
            ok = false;
            break;
        }
        for (int i = 0; i < n; i++) {
            if (glyphs[i] == 0xFFFF) {
                ok = false;
                break;
            }
        }
        if (!ok) break;
        p += n;
    }
    SelectObject(dc, old);
    return ok;
}

// ---------------------------------------------------------------------------
// GDI+ text path (no grid fitting)
//
// GDI's TrueType rasterizer snaps strokes to integer pixels.  The embedded
// MiSans subset carries no hinting instructions (no fpgm/cvt), so GDI runs
// its own auto-hint: stroke width jumps discontinuously as the font size
// changes (measured on this font: the 14px step drops 22%) and small text
// goes soft after the luminance->alpha round trip below.  GDI+
// TextRenderingHintAntiAlias has no grid fitting, so strokes stay linear
// with size.  This is the same switch Hydrogen-Keyboard made after the
// identical complaint.
// ---------------------------------------------------------------------------

static float FontEmPx(HFONT f)
{
    LOGFONTW lf = {};
    if (!f || GetObjectW(f, sizeof(lf), &lf) == 0) return 0.0f;
    return (float)(lf.lfHeight < 0 ? -lf.lfHeight : lf.lfHeight);
}

// Latin lowercase with descenders: their ink already sits low in the line
// box, so the vertical compensation below must be computed differently.
static bool TextHasDescender(const wchar_t* s)
{
    if (!s) return false;
    for (const wchar_t* p = s; *p; ++p) {
        wchar_t c = *p;
        if (c == L'g' || c == L'j' || c == L'p' || c == L'q' || c == L'y') return true;
    }
    return false;
}

// GDI+ LineAlignment=Center centers the line box (MiSans: asc+desc =
// 1.326em), not the ink.  CJK and capless Latin ink sits high in that box,
// so a single line must be shifted down by this much to look centered.
static int TextInkShiftPx(float emPx, const wchar_t* s)
{
    if (emPx <= 0.0f) return 0;
    float v = emPx * (TextHasDescender(s) ? -0.025f : 0.085f);
    return (int)(v >= 0.0f ? v + 0.5f : v - 0.5f);
}

// Draw one line straight onto the 32bpp PARGB surface with GDI+.  Returns
// false when GDI+ cannot serve this line (private font not ready, family
// unavailable, or the line does not fit its rectangle), so the caller uses
// the GDI mask path instead.
static bool DrawTextGpLine(Gdiplus::Graphics& g, const wchar_t* text, float emPx,
    const Gdiplus::Color& color, int align, double dpiScale,
    int x, int y, int w, int h)
{
    if (!text || !text[0] || w <= 0 || h <= 0 || emPx <= 0.0f) return false;
    if (!g_gdipFonts) return false;

    Gdiplus::FontFamily family(g_fontReady ? L"MiSans" : L"Microsoft YaHei",
        g_gdipFonts);
    if (!family.IsAvailable()) return false;
    Gdiplus::Font font(&family, emPx, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    if (font.GetLastStatus() != Gdiplus::Ok) return false;

    // GDI+ lays text out with roughly 0.2em of slack per side and, under
    // NoWrap, DROPS trailing characters (it does not clip) when the
    // rectangle is narrower than that.  Measure with the same flags first;
    // lines that do not fit take the caller's GDI ellipsis path.
    Gdiplus::StringFormat mfmt;
    mfmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap |
                        Gdiplus::StringFormatFlagsNoClip);
    Gdiplus::RectF box(0.0f, 0.0f, 4096.0f, emPx * 4.0f), need;
    if (g.MeasureString(text, -1, &font, box, &mfmt, &need) != Gdiplus::Ok)
        return false;
    if ((int)need.Width + 1 > w) return false;

    // High DPI: no grid fitting keeps stroke width linear with size.
    // Small text at low DPI needs the grid-fit variant to stay legible.
    g.SetTextRenderingHint(dpiScale >= 1.5
        ? Gdiplus::TextRenderingHintAntiAlias
        : Gdiplus::TextRenderingHintAntiAliasGridFit);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    Gdiplus::StringFormat fmt;
    fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoClip |
                       Gdiplus::StringFormatFlagsNoWrap);
    fmt.SetAlignment(align == 1 ? Gdiplus::StringAlignmentCenter
                   : align == 2 ? Gdiplus::StringAlignmentFar
                                : Gdiplus::StringAlignmentNear);
    fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    Gdiplus::SolidBrush brush(color);
    Gdiplus::RectF rc((Gdiplus::REAL)x, (Gdiplus::REAL)y,
                      (Gdiplus::REAL)w, (Gdiplus::REAL)h);
    rc.Y += (Gdiplus::REAL)TextInkShiftPx(emPx, text);
    g.DrawString(text, -1, &font, rc, &fmt, &brush);
    return true;
}

// Render one text line.  Preferred path: GDI+ straight onto the layered
// surface (no grid fitting, no luminance->alpha round trip).  Fallback:
// GDI on an opaque mask, derive per-pixel alpha from the luminance
// difference, then composite - used when the private font is not ready,
// the text contains glyphs outside the subset (whole line falls back to
// the system CJK font), or the line does not fit its rectangle (GDI+
// would drop trailing characters; GDI DrawTextW ellipsizes instead).
static void DrawTextGdiLine(Gdiplus::Graphics& g, const wchar_t* text, int px,
    int weight, const Gdiplus::Color& color, bool lightText,
    int x, int y, int w, int h,
    UINT fmt = DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX,
    double dpiScale = -1.0)
{
    if (!text || !text[0] || w <= 0 || h <= 0) return;
    if (dpiScale <= 0.0) dpiScale = g_scale;

    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC screen = GetDC(NULL);
    HDC dc = CreateCompatibleDC(screen);
    ReleaseDC(NULL, screen);
    if (!dc) return;

    void* bits = NULL;
    HBITMAP hbm = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!hbm || !bits) {
        DeleteDC(dc);
        return;
    }

    BYTE bg = lightText ? 0 : 255;
    {
        DWORD fill = (DWORD)bg | ((DWORD)bg << 8) | ((DWORD)bg << 16) | 0xFF000000u;
        DWORD* p = (DWORD*)bits;
        int total = w * h;
        for (int i = 0; i < total; i++) p[i] = fill;
    }

    HBITMAP oldBmp = (HBITMAP)SelectObject(dc, hbm);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(color.GetR(), color.GetG(), color.GetB()));
    // Grayscale AA, not ClearType: this buffer is turned into per-pixel alpha
    // below, and subpixel coverage cannot survive that conversion - it only
    // spreads each stem into a grey fringe. ClearType would also be pointless
    // once the window is composited with alpha.
    HFONT hf = CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        g_fontReady ? L"MiSans" : L"Microsoft YaHei");
    if (g_fontReady && hf && !FontCoversText(dc, hf, text)) {
        DeleteObject(hf);
        hf = CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    }
    if (!hf) {
        SelectObject(dc, oldBmp);
        DeleteObject(hbm);
        DeleteDC(dc);
        return;
    }

    // Preferred: GDI+ straight onto the surface.  Only for the embedded
    // face - the YaHei fallback line (glyphs outside the subset) stays on
    // the GDI mask path so the whole line keeps one consistent face.
    if (g_fontReady) {
        int align = (fmt & DT_CENTER) ? 1 : (fmt & DT_RIGHT) ? 2 : 0;
        if (DrawTextGpLine(g, text, FontEmPx(hf), color, align, dpiScale,
                x, y, w, h)) {
            SelectObject(dc, oldBmp);
            DeleteObject(hbm);
            DeleteObject(hf);
            DeleteDC(dc);
            return;
        }
    }

    HGDIOBJ oldFont = SelectObject(dc, hf);
    RECT rect;
    rect.left = 0;
    rect.top = 0;
    rect.right = w;
    rect.bottom = h;
    DrawTextW(dc, text, -1, &rect, fmt);
    SelectObject(dc, oldFont);
    DeleteObject(hf);

    int bl = bg;
    int fl = (color.GetR() * 299 + color.GetG() * 587 + color.GetB() * 114) / 1000;
    int denom = bl - fl;
    if (denom == 0) denom = 1;

    BYTE* p = (BYTE*)bits;
    int total = w * h;
    for (int i = 0; i < total; i++) {
        BYTE* q = p + i * 4;
        int lum = (q[2] * 299 + q[1] * 587 + q[0] * 114) / 1000;
        int a = (bl - lum) * 255 / denom;
        if (a < 0) a = 0;
        if (a > 255) a = 255;
        q[0] = (BYTE)(color.GetB() * a / 255);
        q[1] = (BYTE)(color.GetG() * a / 255);
        q[2] = (BYTE)(color.GetR() * a / 255);
        q[3] = (BYTE)a;
    }

    Gdiplus::Bitmap line(w, h, w * 4, PixelFormat32bppPARGB, (BYTE*)bits);
    if (line.GetLastStatus() == Gdiplus::Ok)
        g.DrawImage(&line, x, y);

    SelectObject(dc, oldBmp);
    DeleteObject(hbm);
    DeleteDC(dc);
}

static int GetEncoderClsid(const wchar_t* mime, CLSID* clsid)
{
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    Gdiplus::ImageCodecInfo* codecs = (Gdiplus::ImageCodecInfo*)malloc(size);
    if (!codecs) return -1;
    Gdiplus::GetImageEncoders(num, size, codecs);
    int found = -1;
    for (UINT i = 0; i < num; i++) {
        if (wcscmp(codecs[i].MimeType, mime) == 0) {
            *clsid = codecs[i].Clsid;
            found = (int)i;
            break;
        }
    }
    free(codecs);
    return found;
}

static void DumpPanelIfRequested(const void* bits)
{
    wchar_t path[MAX_PATH] = { 0 };
    if (g_dumpPath[0]) {
        wcsncpy_s(path, MAX_PATH, g_dumpPath, _TRUNCATE);
    } else {
        DWORD n = GetEnvironmentVariableW(L"HSPEC_DUMP", path, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return;
    }
    if (!bits) return;
    CLSID png;
    if (GetEncoderClsid(L"image/png", &png) < 0) return;
    Gdiplus::Bitmap out(g_winW, g_winH, g_winW * 4, PixelFormat32bppPARGB,
        (BYTE*)bits);
    if (out.GetLastStatus() == Gdiplus::Ok)
        out.Save(path, &png, NULL);
}

static bool RenderInternal(void)
{
    if (!g_hwnd || g_winW <= 0 || g_winH <= 0) return false;
    double s = g_scale;

    Gdiplus::Bitmap bmp(g_winW, g_winH, PixelFormat32bppPARGB);
    if (bmp.GetLastStatus() != Gdiplus::Ok) return false;
    Gdiplus::Graphics g(&bmp);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    g.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    g.Clear(Gdiplus::Color(0, 0, 0, 0));

    const bool card = CardMode();
    const int cardAlpha = ClampInt(g_cfg.opacity, 40, 100) * 255 / 100;
    // Card themes follow 明暗模式; the plain theme keeps its own wallpaper
    // contrast so its text stays readable over any background.
    bool darkCard = card ? EffectiveDark() : g_lightText;
    Palette pal = MakePalette(darkCard, cardAlpha);

    const Gdiplus::Color titleColor = card ? pal.title : g_titleColor;
    const Gdiplus::Color detailColor = card ? pal.detail : g_detailColor;
    const Gdiplus::Color iconColor = card ? pal.chipFg : g_iconColor;
    const bool lightText = card ? pal.darkCard : g_lightText;

    Gdiplus::RectF cardRect((float)g_cardMargin, (float)g_cardMargin,
        (float)(g_winW - g_cardMargin * 2), (float)(g_winH - g_cardMargin * 2));
    float radius = (float)g_cfg.radius * (float)s;
    if (radius > cardRect.Height * 0.5f) radius = cardRect.Height * 0.5f;

    const bool split = (g_cfg.theme == THEME_SPLIT);
    if (card && !split) {
        DrawPanelShadow(g, cardRect, radius);
        DrawPanelCard(g, cardRect, radius, pal);
    }

    Gdiplus::Pen pen(iconColor, (Gdiplus::REAL)(1.5 * s));
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);

    int titlePx = (int)(16.0 * s + 0.5);
    int detailPx = (int)(14.0 * s + 0.5);
    if (titlePx < 10) titlePx = 10;
    if (detailPx < 9) detailPx = 9;

    float y = card ? (float)(g_cardMargin + g_padY) : (float)g_padTop;
    if (split) y = (float)g_cardMargin + (float)g_padY;
    float textX = card
        ? (float)(g_cardMargin + g_padX + g_chipSize + g_gapX)
        : (float)(g_iconSize + g_gapX);
    float textW = (float)g_winW - textX - (float)(card ? g_cardMargin + g_padX : 4.0 * s);

    for (int i = 0; i < g_rowCount; i++) {
        UiRow& row = g_rows[i];
        if (split) {
            // Separated card for this section only.
            float rowH = (float)(g_padY * 2 + g_titleH + row.detailCount * g_detailH);
            Gdiplus::RectF rowCard((float)g_cardMargin, y - (float)g_padY,
                (float)(g_winW - g_cardMargin * 2), rowH);
            float rowRadius = radius;
            if (rowRadius > rowCard.Height * 0.5f) rowRadius = rowCard.Height * 0.5f;
            DrawPanelShadow(g, rowCard, rowRadius);
            DrawPanelCard(g, rowCard, rowRadius, pal);
        }
        if (card) {
            // Tinted chip behind the glyph, like the theme's btn-regular-bg.
            Gdiplus::RectF chip(cardRect.X + g_padX,
                y + ((float)g_titleH - (float)g_chipSize) * 0.5f,
                (float)g_chipSize, (float)g_chipSize);
            Gdiplus::GraphicsPath cp;
            AddRoundRect(cp, chip, (float)g_chipSize * 0.3f);
            Gdiplus::SolidBrush chipBrush(pal.chipBg);
            g.FillPath(&chipBrush, &cp);

            float glyph = (float)g_chipSize * 0.56f;
            DrawGlyph(g, pen, row.icon,
                chip.X + ((float)g_chipSize - glyph) * 0.5f,
                chip.Y + ((float)g_chipSize - glyph) * 0.5f, glyph, (float)s);
        } else {
            float iconY = y + ((float)g_titleH - (float)g_iconSize) * 0.5f;
            DrawGlyph(g, pen, row.icon, 2.0f * (float)s, iconY, (float)g_iconSize, (float)s);
        }
        DrawTextGdiLine(g, row.title, titlePx, FW_NORMAL, titleColor, lightText,
            (int)textX, (int)y, (int)textW, g_titleH);
        y += (float)g_titleH;
        for (int j = 0; j < row.detailCount; j++) {
            DrawTextGdiLine(g, row.details[j], detailPx, FW_NORMAL, detailColor,
                lightText, (int)textX, (int)y, (int)textW, g_detailH);
            y += (float)g_detailH;
        }
        if (split) {
            // next card's text starts 2*padY below this card's text end
            // (this card's bottom pad + the next card's top pad), plus the
            // inter-card gap.
            y += (float)g_padY * 2.0f;
            if (i + 1 < g_rowCount) y += (float)g_rowGap;
        } else {
            y += (float)g_rowGap;
        }
    }

    // Bitmap::GetHBITMAP() drops the alpha channel for PARGB bitmaps, so copy
    // the premultiplied pixels into our own 32bpp top-down DIB section.
    Gdiplus::BitmapData bd;
    Gdiplus::Rect lockRect(0, 0, g_winW, g_winH);
    if (bmp.LockBits(&lockRect, Gdiplus::ImageLockModeRead,
            PixelFormat32bppPARGB, &bd) != Gdiplus::Ok)
        return false;

    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = g_winW;
    bi.bmiHeader.biHeight = -g_winH;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = NULL;
    HBITMAP hbm = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (hbm && bits) {
        const BYTE* src = (const BYTE*)bd.Scan0;
        int rowBytes = g_winW * 4;
        if (bd.Stride < 0) {
            for (int y = 0; y < g_winH; y++)
                memcpy((BYTE*)bits + y * rowBytes,
                    src + (g_winH - 1 - y) * (-bd.Stride), rowBytes);
        } else {
            for (int y = 0; y < g_winH; y++)
                memcpy((BYTE*)bits + y * rowBytes, src + y * bd.Stride, rowBytes);
        }
    }
    bmp.UnlockBits(&bd);
    if (!hbm) return false;

    DumpPanelIfRequested(bits);

    HDC screen = GetDC(NULL);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP old = (HBITMAP)SelectObject(mem, hbm);

    BLENDFUNCTION bf;
    bf.BlendOp = AC_SRC_OVER;
    bf.BlendFlags = 0;
    bf.SourceConstantAlpha = (BYTE)g_hoverAlpha;
    bf.AlphaFormat = AC_SRC_ALPHA;

    SIZE size;
    size.cx = g_winW;
    size.cy = g_winH;
    POINT src = { 0, 0 };
    BOOL ok = UpdateLayeredWindow(g_hwnd, screen, NULL, &size, mem, &src, 0, &bf, ULW_ALPHA);

    SelectObject(mem, old);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
    // Keep the DIB: the hover fade re-blends it instead of re-rendering.
    if (g_pushBmp) DeleteObject(g_pushBmp);
    g_pushBmp = hbm;
    return ok != FALSE;
}

// Re-push the cached frame at the current hover alpha (fade steps).
static void PushHoverAlpha(void)
{
    if (!g_hwnd || !g_pushBmp) return;
    HDC screen = GetDC(NULL);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP old = (HBITMAP)SelectObject(mem, g_pushBmp);
    BLENDFUNCTION bf;
    bf.BlendOp = AC_SRC_OVER;
    bf.BlendFlags = 0;
    bf.SourceConstantAlpha = (BYTE)g_hoverAlpha;
    bf.AlphaFormat = AC_SRC_ALPHA;
    SIZE size;
    size.cx = g_winW;
    size.cy = g_winH;
    POINT src = { 0, 0 };
    UpdateLayeredWindow(g_hwnd, screen, NULL, &size, mem, &src, 0, &bf, ULW_ALPHA);
    SelectObject(mem, old);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
}

static void PositionWindow(void);

static void Render(void)
{
    if (g_hwnd) RenderInternal();
}

// ===========================================================================
// Desktop layer placement
// ===========================================================================
//
// A desktop widget must sit above the wallpaper/icons (Progman + WorkerW)
// but below every ordinary window, and it must never be topmost.  The
// reference tools (WinosInfo) use exactly this: a normal WS_POPUP layered
// window whose z-order is anchored right above the desktop stack.

struct TopWindowList { HWND hwnds[512]; int count; };

static BOOL CALLBACK CollectTopProc(HWND hwnd, LPARAM lp)
{
    TopWindowList* list = (TopWindowList*)lp;
    if (list->count < 512 && IsWindowVisible(hwnd))
        list->hwnds[list->count++] = hwnd;
    return TRUE;
}

static void SendToDesktopLayer(void)
{
    if (!g_hwnd) return;

    TopWindowList list;
    list.count = 0;
    EnumWindows(CollectTopProc, (LPARAM)&list);

    int desk = -1;
    for (int i = 0; i < list.count; i++) {
        wchar_t cls[64] = { 0 };
        GetClassNameW(list.hwnds[i], cls, 64);
        if (wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0) {
            desk = i;
            break;  // topmost desktop-layer window (icons/wallpaper stack)
        }
    }

    if (desk > 0) {
        HWND above = list.hwnds[desk - 1];
        if (above == g_hwnd) return;
        SetWindowPos(g_hwnd, above, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    SetWindowPos(g_hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

static void PositionWindow(void)
{
    if (!g_hwnd) return;
    POINT origin = { 0, 0 };
    HMONITOR mon = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(mon, &mi);

    int marginX = PanelMarginX();
    int marginY = PanelMarginY();
    int x = mi.rcMonitor.right - g_winW - marginX + (int)(g_cfg.posDx * g_scale + 0.5);
    int y = mi.rcMonitor.top + marginY + (int)(g_cfg.posDy * g_scale + 0.5);
    if (x < mi.rcMonitor.left) x = mi.rcMonitor.left;
    if (y < mi.rcMonitor.top) y = mi.rcMonitor.top;
    if (x + g_winW > mi.rcMonitor.right) x = mi.rcMonitor.right - g_winW;
    if (y + g_winH > mi.rcMonitor.bottom) y = mi.rcMonitor.bottom - g_winH;

    SetWindowPos(g_hwnd, NULL, x, y, g_winW, g_winH,
        SWP_NOZORDER | SWP_NOACTIVATE);
}

// ===========================================================================
// Tray icon
// ===========================================================================

static void AddTrayIcon(HWND hwnd, HINSTANCE inst)
{
    if (g_trayAdded || !hwnd) return;
    memset(&g_nid, 0, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_APP + 2;
    int cx = GetSystemMetrics(SM_CXSMICON);
    int cy = GetSystemMetrics(SM_CYSMICON);
    g_nid.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_MAIN_ICON),
        IMAGE_ICON, cx, cy, 0);
    if (!g_nid.hIcon)
        g_nid.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_MAIN_ICON));
    wcscpy_s(g_nid.szTip, 128, L"HSpec 轻谱");
    g_trayAdded = Shell_NotifyIconW(NIM_ADD, &g_nid) != FALSE;
}

static void RemoveTrayIcon(void)
{
    if (g_trayAdded) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        g_trayAdded = false;
    }
    if (g_nid.hIcon) {
        DestroyIcon(g_nid.hIcon);
        g_nid.hIcon = NULL;
    }
}

static void OpenSettings(void);
static void OpenSettingsPage(int page);

static void ShowTrayMenu(HWND hwnd)
{
    POINT pt;
    GetCursorPos(&pt);
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, 3, L"设置…");
    AppendMenuW(menu, MF_STRING, 1, L"刷新");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, 2, L"退出");
    SetForegroundWindow(hwnd);
    int cmd = (int)TrackPopupMenu(menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(menu);
    PostMessageW(hwnd, WM_NULL, 0, 0);

    if (cmd == 1) {
        BuildModel();
        ComputeLayout();
        g_wallpaperStamp = WallpaperStamp();
        UpdateTheme();
        PositionWindow();
        Render();
        SendToDesktopLayer();
    } else if (cmd == 2) {
        DestroyWindow(hwnd);
    } else if (cmd == 3) {
        // 右键菜单的「设置…」直接落到二级设置页，而不是主界面
        OpenSettingsPage(0);
    }
}

// ===========================================================================
// Settings window
// ===========================================================================
//
// The panel itself is WS_EX_TRANSPARENT (clicks fall through to the desktop),
// so it can never host controls. This is a separate top-level layered window
// that paints everything itself and keeps its own hit-test table: no child
// HWNDs, no owner-draw plumbing, and identical typography to the panel
// (same MiSans subset, same ClearType-then-alpha trick).

static const wchar_t* kAppVersion = L"1.0";

static const wchar_t* kSettingsClass = L"HSpecSettings";
static HWND g_setHwnd = NULL;
static int g_setW = 0, g_setH = 0;
static UINT g_setDpi = 96;
// Effective dp->px scale for the settings window: the monitor DPI ratio,
// possibly reduced so the window always fits the work area.
static double g_setScale = 1.0;
static double g_dumpZoom = 1.0;   // --zoom N: enlarge the offscreen render for icon inspection

static double SettingsScale(void) { return g_setScale; }

// DPI of the monitor the window will live on. Reading the panel's own DPI is
// not enough: right after startup the panel window may still be on its initial
// 96-DPI context, which would open the settings window at half size.
static UINT MonitorDpi(HMONITOR mon)
{
    typedef HRESULT(WINAPI * PFN_GetDpiForMonitor)(HMONITOR, int, UINT*, UINT*);
    static PFN_GetDpiForMonitor fn = NULL;
    static bool tried = false;
    if (!tried) {
        tried = true;
        HMODULE shcore = LoadLibraryW(L"shcore.dll");
        if (shcore) fn = (PFN_GetDpiForMonitor)GetProcAddress(shcore, "GetDpiForMonitor");
    }
    if (fn) {
        UINT dx = 0, dy = 0;
        if (SUCCEEDED(fn(mon, 0 /* MDT_EFFECTIVE_DPI */, &dx, &dy)) && dx > 0) return dx;
    }
    return 0;
}

// Size the settings window for a monitor: 1024x720 dp at that monitor's DPI,
// shrunk proportionally if the result would not fit its work area.  Drawing
// uses the same effective scale (SettingsScale), so window and content scale
// as one piece.
static void SettingsLayoutFor(HMONITOR mon, UINT dpi)
{
    if (!dpi) dpi = MonitorDpi(mon);
    if (!dpi) dpi = 96;
    g_setDpi = dpi;
    double s = (double)dpi / 96.0;
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi)) {
        double availW = (double)(mi.rcWork.right - mi.rcWork.left) - 24.0;
        double availH = (double)(mi.rcWork.bottom - mi.rcWork.top) - 24.0;
        if (availW > 1.0 && 1024.0 * s > availW) s = availW / 1024.0;
        if (availH > 1.0 && 760.0 * s > availH) s = availH / 760.0;
    }
    if (s < 0.5) s = 0.5;
    s *= g_dumpZoom;
    g_setScale = s;
    g_setW = (int)(1024.0 * s + 0.5);
    g_setH = (int)(760.0 * s + 0.5);
}
static int g_setPage = 0;       // 0 look, 1 content, 2 about（设置内的分页）
static int g_setMode = 0;       // 0 主界面（硬件显示/系统信息），1 设置（二级）
static int g_mainTab = 0;       // 主界面分页：0 硬件显示，1 目标系统信息
static int g_setHot = 0;
static int g_setDrag = 0;
static Gdiplus::Bitmap* g_logoLight = NULL;
static Gdiplus::Bitmap* g_logoDark = NULL;

enum
{
    ACT_NONE = 0,
    ACT_CLOSE = 1,
    ACT_NAV = 10,        // + page index
    ACT_THEME = 20,      // + theme index
    ACT_HUE_FOLLOW = 30,
    ACT_HUE_STRIP = 31,
    ACT_OPACITY = 32,
    ACT_RADIUS = 33,
    ACT_LIGHT = 34,      // + 0 follow / 1 light / 2 dark
    ACT_SWATCH = 36,     // + 0 wallpaper, 1..5 preset hue
    ACT_TOGGLE = 40,     // + toggle index
    ACT_GITHUB = 50,
    ACT_COPYVER = 51,
    ACT_POS = 60,        // + 0 up / 1 down / 2 left / 3 right / 4 reset
    ACT_SAVEINFO = 70,   // 保存所有信息
    ACT_OPENFOLDER = 71, // 打开保存目录
    ACT_SHOWIP = 72,     // IP 地址查看
    ACT_HELP = 73,       // 使用说明
    ACT_BACK = 74,       // 返回上级
    ACT_OPEN_SET = 80,   // 进入设置（二级页面）
    ACT_SETNAV = 90,     // 设置内的分页 + index
};

enum { TOGGLE_CPU = 0, TOGGLE_BOARD, TOGGLE_MEM, TOGGLE_DISK, TOGGLE_GPU, TOGGLE_NET, TOGGLE_COUNT };

struct HitRect { RECT rc; int act; };
static HitRect g_hits[48];
static int g_hitCount = 0;
static RECT g_hueTrack, g_opTrack, g_radTrack;

static void HitAdd(int x, int y, int w, int h, int act)
{
    if (g_hitCount >= 48) return;
    g_hits[g_hitCount].rc.left = x;
    g_hits[g_hitCount].rc.top = y;
    g_hits[g_hitCount].rc.right = x + w;
    g_hits[g_hitCount].rc.bottom = y + h;
    g_hits[g_hitCount].act = act;
    g_hitCount++;
}

static int HitTest(int x, int y)
{
    for (int i = 0; i < g_hitCount; i++)
        if (PtInRect(&g_hits[i].rc, POINT{ x, y })) return g_hits[i].act;
    return ACT_NONE;
}

static int ToggleValue(int index)
{
    switch (index) {
    case TOGGLE_CPU: return g_cfg.showCpu;
    case TOGGLE_BOARD: return g_cfg.showBoard;
    case TOGGLE_MEM: return g_cfg.showMem;
    case TOGGLE_DISK: return g_cfg.showDisk;
    case TOGGLE_GPU: return g_cfg.showGpu;
    case TOGGLE_NET: return g_cfg.showNet;
    }
    return 0;
}

static void ToggleFlip(int index)
{
    switch (index) {
    case TOGGLE_CPU: g_cfg.showCpu = !g_cfg.showCpu; break;
    case TOGGLE_BOARD: g_cfg.showBoard = !g_cfg.showBoard; break;
    case TOGGLE_MEM: g_cfg.showMem = !g_cfg.showMem; break;
    case TOGGLE_DISK: g_cfg.showDisk = !g_cfg.showDisk; break;
    case TOGGLE_GPU: g_cfg.showGpu = !g_cfg.showGpu; break;
    case TOGGLE_NET: g_cfg.showNet = !g_cfg.showNet; break;
    default: return;
    }
}

static bool g_cfgDirty = false;

// Fast preview path for slider drags: repaint only.  No disk writes, no
// EnumWindows, no wallpaper resampling (UpdateTheme is cached anyway) - the
// drag tick must stay instant.
static void PanelPreview(void)
{
    if (!g_hwnd) return;
    ComputeLayout();
    Render();
    g_cfgDirty = true;
}

static void PanelReload(bool rebuildModel)
{
    if (g_hwnd) {
        if (rebuildModel) BuildModel();
        ComputeLayout();
        UpdateTheme();
        PositionWindow();
        Render();
        SendToDesktopLayer();
    }
    SaveConfig();
    g_cfgDirty = false;
}

// Persist settings that were only previewed during a drag.
static void PanelCommit(void)
{
    if (!g_cfgDirty) return;
    SaveConfig();
    g_cfgDirty = false;
}

// ---------------------------------------------------------------------------
// Drawing primitives
// ---------------------------------------------------------------------------

static Gdiplus::RectF MRect(float x, float y, float w, float h)
{
    return Gdiplus::RectF(x, y, w, h);
}

static void FillRound(Gdiplus::Graphics& g, const Gdiplus::RectF& rc, float r,
    const Gdiplus::Color& c)
{
    Gdiplus::GraphicsPath p;
    AddRoundRect(p, rc, r);
    Gdiplus::SolidBrush b(c);
    g.FillPath(&b, &p);
}

static void SText(Gdiplus::Graphics& g, const wchar_t* text, float dp, int weight,
    const Gdiplus::Color& c, bool light, float x, float y, float w, float h,
    UINT fmt = DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX)
{
    double s = SettingsScale();
    int px = (int)(dp * s + 0.5);
    if (px < 9) px = 9;
    DrawTextGdiLine(g, text, px, weight, c, light,
        (int)(x + 0.5f), (int)(y + 0.5f), (int)(w + 0.5f), (int)(h + 0.5f), fmt, s);
}

static bool SettingsPush(void);

static void DrawSwitch(Gdiplus::Graphics& g, const Gdiplus::RectF& rc, bool on,
    const Palette& pal)
{
    float h = rc.Height;
    FillRound(g, rc, h * 0.5f, on ? pal.primary : pal.chipBg);
    float k = h - 5.0f;
    float kx = on ? rc.GetRight() - k - 2.5f : rc.X + 2.5f;
    Gdiplus::SolidBrush kb(Gdiplus::Color(255, 255, 255, 255));
    g.FillEllipse(&kb, kx, rc.Y + 2.5f, k, k);
}

static void DrawSlider(Gdiplus::Graphics& g, const Gdiplus::RectF& rc, float t,
    const Palette& pal, bool darkPage)
{
    float cy = rc.Y + rc.Height * 0.5f;
    Gdiplus::Pen track(pal.chipBg, 4.0f);
    track.SetStartCap(Gdiplus::LineCapRound);
    track.SetEndCap(Gdiplus::LineCapRound);
    g.DrawLine(&track, rc.X + 2, cy, rc.GetRight() - 2, cy);

    float knobX = rc.X + 2 + (rc.Width - 4) * (t < 0 ? 0 : (t > 1 ? 1 : t));
    if (knobX > rc.X + 2) {
        Gdiplus::Pen fill(pal.primary, 4.0f);
        fill.SetStartCap(Gdiplus::LineCapRound);
        fill.SetEndCap(Gdiplus::LineCapRound);
        g.DrawLine(&fill, rc.X + 2, cy, knobX, cy);
    }
    Gdiplus::SolidBrush kb(Gdiplus::Color(255, 255, 255, 255));
    g.FillEllipse(&kb, knobX - 6.5f, cy - 6.5f, 13.0f, 13.0f);
    Gdiplus::Pen ring(darkPage ? Gdiplus::Color(40, 0, 0, 0) : Gdiplus::Color(32, 0, 0, 0), 1.0f);
    g.DrawEllipse(&ring, knobX - 6.5f, cy - 6.5f, 13.0f, 13.0f);
}

static void DrawHueStrip(Gdiplus::Graphics& g, const Gdiplus::RectF& rc, float hue,
    const Palette& pal)
{
    // 13 OKLCH stops, same sweep the theme's hue control uses.
    Gdiplus::GraphicsPath path;
    AddRoundRect(path, rc, rc.Height * 0.5f);
    Gdiplus::Region saved;
    g.GetClip(&saved);
    g.SetClip(&path);
    const int stops = 12;
    for (int i = 0; i < stops; i++) {
        float x0 = rc.X + rc.Width * i / stops;
        float x1 = rc.X + rc.Width * (i + 1) / stops;
        Gdiplus::Color c0 = OklchColor(0.72, 0.16, i * 30.0, 255);
        Gdiplus::Color c1 = OklchColor(0.72, 0.16, (i + 1) * 30.0, 255);
        Gdiplus::LinearGradientBrush brush(
            Gdiplus::PointF(x0, rc.Y), Gdiplus::PointF(x1, rc.Y), c0, c1);
        g.FillRectangle(&brush, x0, rc.Y, x1 - x0 + 1, rc.Height);
    }
    g.SetClip(&saved);

    float t = hue / 360.0f;
    float kx = rc.X + rc.Width * t;
    Gdiplus::SolidBrush kb(Gdiplus::Color(255, 255, 255, 255));
    g.FillEllipse(&kb, kx - 8.5f, rc.Y + rc.Height * 0.5f - 8.5f, 17.0f, 17.0f);
    Gdiplus::Pen ring(pal.primary, 3.0f);
    g.DrawEllipse(&ring, kx - 7.0f, rc.Y + rc.Height * 0.5f - 7.0f, 14.0f, 14.0f);
}

// Procedural theme thumbnails: no PNG assets, they follow the current hue.
static void DrawThemeThumb(Gdiplus::Graphics& g, const Gdiplus::RectF& rc, int kind,
    const Palette& pal, float s)
{
    // Theme ids changed when 轻卡片 was removed: 1 (卡片) draws like the old
    // ethereal thumb (kind 2 below), 2 (分离卡片) like the old split (kind 3).
    int k = (kind == 1) ? 2 : (kind == 2) ? 3 : 0;
    kind = k;
    Gdiplus::GraphicsPath path;
    AddRoundRect(path, rc, 9 * s);
    Gdiplus::Region saved;
    g.GetClip(&saved);
    g.SetClip(&path);

    // Miniature wallpaper: two soft blobs over a diagonal gradient.
    {
        Gdiplus::LinearGradientBrush sky(Gdiplus::PointF(rc.X, rc.Y),
            Gdiplus::PointF(rc.GetRight(), rc.GetBottom()),
            Gdiplus::Color(255, 84, 110, 158), Gdiplus::Color(255, 62, 74, 108));
        g.FillRectangle(&sky, rc);
        Gdiplus::SolidBrush warm(Gdiplus::Color(120, 214, 158, 96));
        g.FillEllipse(&warm, rc.X + rc.Width * 0.55f, rc.Y + rc.Height * 0.45f,
            rc.Width * 0.7f, rc.Height * 0.9f);
        Gdiplus::SolidBrush cool(Gdiplus::Color(90, 90, 140, 210));
        g.FillEllipse(&cool, rc.X - rc.Width * 0.2f, rc.Y - rc.Height * 0.15f,
            rc.Width * 0.75f, rc.Height * 0.8f);
    }

    float inset = 9 * s;
    Gdiplus::RectF sheet(rc.X + inset, rc.Y + inset,
        rc.Width - inset * 2, rc.Height - inset * 2);

    bool light = pal.darkCard;
    Gdiplus::Color barStrong = light ? Gdiplus::Color(200, 255, 255, 255)
                                    : Gdiplus::Color(190, 24, 26, 32);
    Gdiplus::Color barWeak = light ? Gdiplus::Color(110, 255, 255, 255)
                                  : Gdiplus::Color(90, 24, 26, 32);

    if (kind == 0) {
        // Pure text: bars land straight on the wallpaper.
        float lh = 3 * s, gap = 9 * s;
        for (int i = 0; i < 3; i++) {
            float y = rc.Y + (14 + i * gap) * s;
            FillRound(g, MRect(rc.X + 11 * s, y, (60 - i * 6) * s, 3.6f * s), 2 * s, barStrong);
            FillRound(g, MRect(rc.X + 11 * s, y + 7 * s, (86 - i * 10) * s, 2.6f * s), 1.5f * s, barWeak);
            (void)lh;
        }
        g.SetClip(&saved);
        return;
    }

    if (kind == 3) {
        // Split cards: three separate rounded sheets stacked on the wallpaper.
        float cardH = (sheet.Height - 6 * s) / 3.0f;
        for (int i = 0; i < 3; i++) {
            Gdiplus::RectF one(sheet.X, sheet.Y + i * (cardH + 3 * s),
                sheet.Width, cardH);
            Gdiplus::SolidBrush sheetBrush(Gdiplus::Color(196, 255, 255, 255));
            Gdiplus::GraphicsPath sp;
            AddRoundRect(sp, one, 5 * s);
            g.FillPath(&sheetBrush, &sp);
            float y = one.Y + 3.5f * s;
            FillRound(g, MRect(one.X + 5 * s, y, 8 * s, 8 * s), 2.5f * s, pal.chipBg);
            float tx = one.X + 16 * s;
            FillRound(g, MRect(tx, y + 0.5f * s, 52 * s - i * 5 * s, 3.2f * s), 2 * s, barStrong);
            FillRound(g, MRect(tx, y + 5.5f * s, 34 * s - i * 4 * s, 2.3f * s), 1.5f * s, barWeak);
        }
        g.SetClip(&saved);
        Gdiplus::Pen edge(Gdiplus::Color(28, 0, 0, 0), 1.0f);
        g.DrawPath(&edge, &path);
        return;
    }

    if (kind == 1) {
        Gdiplus::SolidBrush sheetBrush(Gdiplus::Color(255, 255, 255, 255));
        Gdiplus::GraphicsPath sp;
        AddRoundRect(sp, sheet, 6 * s);
        g.FillPath(&sheetBrush, &sp);
    } else {
        Gdiplus::SolidBrush sheetBrush(Gdiplus::Color(196, 255, 255, 255));
        Gdiplus::GraphicsPath sp;
        AddRoundRect(sp, sheet, 7 * s);
        g.FillPath(&sheetBrush, &sp);
    }

    for (int i = 0; i < 3; i++) {
        float y = rc.Y + (13 + i * 16) * s;
        if (kind == 2) {
            FillRound(g, MRect(sheet.X + 4 * s, y, 9 * s, 9 * s), 3 * s, pal.chipBg);
        }
        float tx = sheet.X + (kind == 2 ? 17.0f : 6.5f) * s;
        float tw = (kind == 2 ? 62.0f : 74.0f) * s - i * 4 * s;
        FillRound(g, MRect(tx, y, tw, 3.6f * s), 2 * s, barStrong);
        FillRound(g, MRect(tx, y + 6.5f * s, tw * 0.66f, 2.6f * s), 1.5f * s, barWeak);
    }

    g.SetClip(&saved);
    Gdiplus::Pen edge(Gdiplus::Color(28, 0, 0, 0), 1.0f);
    g.DrawPath(&edge, &path);
}

// Linear icon set on a 24dp grid: strokes scale with the icon size, round caps and
// joins, shapes kept inside a 18dp safe area. Matches the Ethereal silhouette rules
// and sits centred in the PanDaPE-style rounded tile.
static void DrawNavIcon(Gdiplus::Graphics& g, int kind, float x, float y, float sz,
    const Gdiplus::Color& c)
{
    const float u = sz / 24.0f;
    Gdiplus::Pen pen(c, 1.8f * u);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    Gdiplus::SolidBrush dot(c);

    if (kind == 0) {
        // 外观：调色板（圆盘 + 右下拇指孔缺口 + 三枚颜料点）
        g.DrawArc(&pen, x + 3.4f * u, y + 3.4f * u, 17.2f * u, 17.2f * u, 54.0f, 342.0f);
        const float pts[3][2] = { { 9.0f, 9.3f }, { 9.7f, 15.0f }, { 14.6f, 9.6f } };
        for (int i = 0; i < 3; i++)
            g.FillEllipse(&dot, x + (pts[i][0] - 1.5f) * u, y + (pts[i][1] - 1.5f) * u,
                3.0f * u, 3.0f * u);
    } else if (kind == 1) {
        // 显示内容：条目列表（圆点 + 行）
        const float rows[3] = { 7.0f, 12.0f, 17.0f };
        for (int i = 0; i < 3; i++) {
            g.FillEllipse(&dot, x + 4.2f * u, y + (rows[i] - 1.5f) * u, 3.0f * u, 3.0f * u);
            g.DrawLine(&pen, x + 9.8f * u, y + rows[i] * u, x + 19.6f * u, y + rows[i] * u);
        }
    } else if (kind == 3) {
        // 系统信息：显示器
        Gdiplus::GraphicsPath scr;
        AddRoundRect(scr, Gdiplus::RectF(x + 3.2f * u, y + 4.4f * u, 17.6f * u, 12.4f * u),
            2.6f * u);
        g.DrawPath(&pen, &scr);
        g.DrawLine(&pen, x + 12.0f * u, y + 16.8f * u, x + 12.0f * u, y + 19.4f * u);
        g.DrawLine(&pen, x + 8.6f * u, y + 19.4f * u, x + 15.4f * u, y + 19.4f * u);
    } else if (kind == 4) {
        // 目标系统信息：资料卡（带折角）
        Gdiplus::GraphicsPath doc;
        doc.AddLine(x + 5.0f * u, y + 3.6f * u, x + 13.4f * u, y + 3.6f * u);
        doc.AddLine(x + 13.4f * u, y + 3.6f * u, x + 19.0f * u, y + 9.2f * u);
        doc.AddLine(x + 19.0f * u, y + 9.2f * u, x + 19.0f * u, y + 20.4f * u);
        doc.AddLine(x + 19.0f * u, y + 20.4f * u, x + 5.0f * u, y + 20.4f * u);
        doc.CloseFigure();
        g.DrawPath(&pen, &doc);
        g.DrawLine(&pen, x + 13.4f * u, y + 3.6f * u, x + 13.4f * u, y + 9.2f * u);
        g.DrawLine(&pen, x + 13.4f * u, y + 9.2f * u, x + 19.0f * u, y + 9.2f * u);
        g.DrawLine(&pen, x + 8.2f * u, y + 13.0f * u, x + 15.8f * u, y + 13.0f * u);
        g.DrawLine(&pen, x + 8.2f * u, y + 16.6f * u, x + 13.4f * u, y + 16.6f * u);
    } else if (kind == 5) {
        // 设置：齿轮
        g.DrawEllipse(&pen, x + 5.6f * u, y + 5.6f * u, 12.8f * u, 12.8f * u);
        Gdiplus::Pen tooth(c, 2.6f * u);
        tooth.SetStartCap(Gdiplus::LineCapRound);
        tooth.SetEndCap(Gdiplus::LineCapRound);
        for (int i = 0; i < 8; i++) {
            double a = i * 3.14159265358979323846 / 4.0;
            float c1 = (float)cos(a), s1 = (float)sin(a);
            g.DrawLine(&tooth, x + (12.0f + c1 * 6.4f) * u, y + (12.0f + s1 * 6.4f) * u,
                x + (12.0f + c1 * 8.6f) * u, y + (12.0f + s1 * 8.6f) * u);
        }
        g.FillEllipse(&dot, x + 10.4f * u, y + 10.4f * u, 3.2f * u, 3.2f * u);
    } else {
        // 关于：信息圆
        g.DrawEllipse(&pen, x + 4.0f * u, y + 4.0f * u, 16.0f * u, 16.0f * u);
        g.FillEllipse(&dot, x + 10.6f * u, y + 6.1f * u, 2.9f * u, 2.9f * u);
        g.DrawLine(&pen, x + 12.0f * u, y + 11.2f * u, x + 12.0f * u, y + 16.4f * u);
    }
}

// ---------------------------------------------------------------------------
// Pages
// ---------------------------------------------------------------------------

// Tiny filled triangle, used for the four move arrows.
static void FillTri(Gdiplus::Graphics& g, const Gdiplus::Color& c,
    float x1, float y1, float x2, float y2, float x3, float y3)
{
    Gdiplus::PointF pts[3] = {
        Gdiplus::PointF(x1, y1), Gdiplus::PointF(x2, y2), Gdiplus::PointF(x3, y3)
    };
    Gdiplus::SolidBrush b(c);
    g.FillPolygon(&b, pts, 3);
}

// Circular icon chip used on every settings row (PanDa-style).
static void DrawRowIcon(Gdiplus::Graphics& g, int kind, float cx, float cy,
    float r, const Palette& pal)
{
    Gdiplus::SolidBrush bg(pal.chipBg);
    g.FillEllipse(&bg, cx - r, cy - r, r * 2, r * 2);

    const float u = r / 10.5f;          // 24dp grid, icon fills ~2/3 of the chip
    Gdiplus::Pen pen(pal.chipFg, 1.8f * u);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    const Gdiplus::Color fg = pal.chipFg;

    switch (kind) {
    case 0: {   // 明暗模式：对比圆（右半实心）
        const float rr = 7.2f * u;
        g.DrawEllipse(&pen, cx - rr, cy - rr, rr * 2, rr * 2);
        Gdiplus::GraphicsPath half;
        half.AddArc(cx - rr, cy - rr, rr * 2, rr * 2, -90.0f, 180.0f);
        half.CloseFigure();
        Gdiplus::SolidBrush fill(fg);
        g.FillPath(&fill, &half);
        break;
    }
    case 1: {   // 主题色相：色环 + 顶部实心标记（当前色相位置）
        const float rr = 6.6f * u;
        Gdiplus::Pen ring(fg, 1.8f * u);
        g.DrawEllipse(&ring, cx - rr, cy - rr, rr * 2, rr * 2);
        Gdiplus::SolidBrush marker(fg);
        g.FillEllipse(&marker, cx - 1.8f * u, cy - rr - 1.7f * u, 3.6f * u, 3.6f * u);
        break;
    }
    case 2: {   // 圆角：一个被磨圆的角（L 形 + 大圆弧）
        Gdiplus::GraphicsPath corner;
        corner.AddLine(cx - 4.4f * u, cy + 7.6f * u, cx - 4.4f * u, cy + 1.2f * u);
        corner.AddArc(cx - 4.4f * u, cy - 5.6f * u, 13.2f * u, 13.2f * u, 180.0f, 90.0f);
        corner.AddLine(cx + 2.2f * u, cy - 5.6f * u, cx + 7.6f * u, cy - 5.6f * u);
        Gdiplus::Pen thick(fg, 2.0f * u);
        thick.SetLineJoin(Gdiplus::LineJoinRound);
        thick.SetStartCap(Gdiplus::LineCapRound);
        thick.SetEndCap(Gdiplus::LineCapRound);
        g.DrawPath(&thick, &corner);
        break;
    }
    case 3: {   // 卡片不透明度：两张错位卡片，重叠区加深
        Gdiplus::GraphicsPath back;
        AddRoundRect(back, Gdiplus::RectF(cx - 7.6f * u, cy - 7.6f * u, 10.8f * u, 10.8f * u),
            3.0f * u);
        g.DrawPath(&pen, &back);

        Gdiplus::GraphicsPath front;
        AddRoundRect(front, Gdiplus::RectF(cx - 3.2f * u, cy - 3.2f * u, 10.8f * u, 10.8f * u),
            3.0f * u);
        Gdiplus::SolidBrush soft(Gdiplus::Color(132, fg.GetR(), fg.GetG(), fg.GetB()));
        g.FillPath(&soft, &front);
        g.DrawPath(&pen, &front);
        break;
    }
    default: {  // 显示位置：十字 + 四向实心箭头
        const float e = 4.6f * u;
        g.DrawLine(&pen, cx - e, cy, cx + e, cy);
        g.DrawLine(&pen, cx, cy - e, cx, cy + e);
        const float tip = 8.0f * u, base = 4.6f * u, hw = 2.8f * u;
        FillTri(g, fg, cx, cy - tip, cx - hw, cy - base, cx + hw, cy - base);
        FillTri(g, fg, cx, cy + tip, cx - hw, cy + base, cx + hw, cy + base);
        FillTri(g, fg, cx - tip, cy, cx - base, cy - hw, cx - base, cy + hw);
        FillTri(g, fg, cx + tip, cy, cx + base, cy - hw, cx + base, cy + hw);
        break;
    }
    }
}

// Segmented control (跟随系统 / 浅色 / 深色 style). Returns per-segment hit rects.
static void DrawSegmented(Gdiplus::Graphics& g, const Gdiplus::RectF& rc,
    const wchar_t* const* labels, int n, int sel, const Palette& pal, bool light,
    int actBase)
{
    double s = SettingsScale();
    FillRound(g, rc, rc.Height * 0.5f, pal.chipBg);
    float segW = rc.Width / (float)n;
    for (int i = 0; i < n; i++) {
        Gdiplus::RectF seg(rc.X + i * segW, rc.Y, segW, rc.Height);
        if (i == sel) {
            Gdiplus::RectF pill(seg.X + 3, seg.Y + 3, seg.Width - 6, seg.Height - 6);
            FillRound(g, pill, pill.Height * 0.5f, pal.card);
            Gdiplus::Pen ring(pal.primary, 1.2f * (float)s);
            Gdiplus::GraphicsPath rp;
            AddRoundRect(rp, pill, pill.Height * 0.5f);
            g.DrawPath(&ring, &rp);
        }
        Gdiplus::Color fg = (i == sel) ? pal.itemFg : pal.detail;
        SText(g, labels[i], 13.5f, FW_NORMAL, fg, light,
            seg.X, seg.Y, seg.Width, seg.Height,
            DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        HitAdd((int)seg.X, (int)seg.Y, (int)seg.Width, (int)seg.Height, actBase + i);
    }
}

// Value pill ("300 °" / "16 px").
static void DrawValuePill(Gdiplus::Graphics& g, const Gdiplus::RectF& rc,
    const wchar_t* text, const Palette& pal, bool light)
{
    FillRound(g, rc, rc.Height * 0.5f, pal.chipBg);
    SText(g, text, 13, FW_NORMAL, pal.itemFg, light, rc.X, rc.Y, rc.Width, rc.Height,
        DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
}

// Outlined action button (上移 / 默认位置 style).
static void DrawTextButton(Gdiplus::Graphics& g, const Gdiplus::RectF& rc,
    const wchar_t* text, const Palette& pal, bool light, bool hot, int act)
{
    double s = SettingsScale();
    if (hot) FillRound(g, rc, 10 * s, pal.itemBg);
    Gdiplus::Pen border(hot ? pal.primary : pal.divider, 1.2f * (float)s);
    Gdiplus::GraphicsPath rp;
    AddRoundRect(rp, rc, 10 * s);
    g.DrawPath(&border, &rp);
    SText(g, text, 14, FW_NORMAL, pal.title, light, rc.X, rc.Y, rc.Width, rc.Height,
        DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    HitAdd((int)rc.X, (int)rc.Y, (int)rc.Width, (int)rc.Height, act);
}

// 小方块箭头按钮（显示位置用）：描边 + 居中三角，和 DrawTextButton 同一套观感。
static void DrawArrowButton(Gdiplus::Graphics& g, const Gdiplus::RectF& rc, int dir,
    const Palette& pal, bool hot, int act)
{
    double s = SettingsScale();
    if (hot) FillRound(g, rc, 8 * s, pal.itemBg);
    Gdiplus::Pen border(hot ? pal.primary : pal.divider, 1.2f * (float)s);
    Gdiplus::GraphicsPath rp;
    AddRoundRect(rp, rc, 8 * s);
    g.DrawPath(&border, &rp);

    const float cx = rc.X + rc.Width * 0.5f, cy = rc.Y + rc.Height * 0.5f;
    const float t = 3.6f * (float)s, b = 2.0f * (float)s;
    Gdiplus::PointF pts[3];
    if (dir == 0) {          // up
        pts[0] = Gdiplus::PointF(cx, cy - t);
        pts[1] = Gdiplus::PointF(cx - t, cy + b);
        pts[2] = Gdiplus::PointF(cx + t, cy + b);
    } else if (dir == 1) {   // down
        pts[0] = Gdiplus::PointF(cx, cy + t);
        pts[1] = Gdiplus::PointF(cx - t, cy - b);
        pts[2] = Gdiplus::PointF(cx + t, cy - b);
    } else if (dir == 2) {   // left
        pts[0] = Gdiplus::PointF(cx - t, cy);
        pts[1] = Gdiplus::PointF(cx + b, cy - t);
        pts[2] = Gdiplus::PointF(cx + b, cy + t);
    } else {                 // right
        pts[0] = Gdiplus::PointF(cx + t, cy);
        pts[1] = Gdiplus::PointF(cx - b, cy - t);
        pts[2] = Gdiplus::PointF(cx - b, cy + t);
    }
    Gdiplus::SolidBrush fg(hot ? pal.primary : pal.title);
    g.FillPolygon(&fg, pts, 3);
    HitAdd((int)rc.X, (int)rc.Y, (int)rc.Width, (int)rc.Height, act);
}

static void PageLook(Gdiplus::Graphics& g, const Palette& pal, bool light,
    float x, float y, float w, float s)
{
    const float pad = 18 * s;
    const float gap = 14 * s;
    const float inner = w - pad * 2;
    Gdiplus::Color sub = pal.detail;

    // ---------------- 1. 主题 ----------------
    const wchar_t* names[3] = { L"纯文字", L"卡片", L"分离卡片" };
    float tileW = (inner - 2 * gap) / 3.0f;
    float thumbH = tileW * 84.0f / 161.0f;
    float cardH1 = pad + 26 * s + 20 * s + 6 * s + thumbH + 6 * s + 22 * s + pad;
    Gdiplus::RectF cardC1(x, y, w, cardH1);
    FillRound(g, cardC1, 14 * s, pal.card);

    SText(g, L"主题", 18, FW_NORMAL, pal.title, light, x + pad, y + pad, inner, 24 * s);
    SText(g, L"同一个信息模型，三种渲染层", 14, FW_NORMAL, sub, light,
        x + pad, y + pad + 26 * s, inner, 20 * s);

    float ty = y + pad + 52 * s;
    for (int i = 0; i < 3; i++) {
        float tx = x + pad + i * (tileW + gap);
        Gdiplus::RectF thumb(tx, ty, tileW, thumbH);
        DrawThemeThumb(g, thumb, i, pal, s);
        if (g_cfg.theme == i) {
            Gdiplus::Pen ring(pal.primary, 2.2f * s);
            Gdiplus::GraphicsPath p;
            AddRoundRect(p, Gdiplus::RectF(thumb.X - 2.5f * s, thumb.Y - 2.5f * s,
                thumb.Width + 5 * s, thumb.Height + 5 * s), 13 * s);
            g.DrawPath(&ring, &p);
        }
        Gdiplus::Color capColor = (g_cfg.theme == i) ? pal.itemFg : sub;
        SText(g, names[i], 14, FW_NORMAL, capColor, light,
            tx, thumb.GetBottom() + 6 * s, tileW - 24 * s, 22 * s);

        float dotR = 6.5f * s;
        float dx = tx + tileW - dotR * 2, dy = thumb.GetBottom() + 6 * s + (22 * s - dotR * 2) * 0.5f;
        Gdiplus::Pen dotPen((g_cfg.theme == i) ? pal.primary : pal.chipBg, 1.8f * s);
        g.DrawEllipse(&dotPen, dx, dy, dotR * 2, dotR * 2);
        if (g_cfg.theme == i) {
            Gdiplus::SolidBrush dotB(pal.primary);
            g.FillEllipse(&dotB, dx + 3.5f * s, dy + 3.5f * s, dotR * 2 - 7 * s, dotR * 2 - 7 * s);
        }
        HitAdd((int)tx, (int)ty, (int)tileW, (int)(thumbH + 30 * s), ACT_THEME + i);
    }

    // ---------------- 2. 主题色（明暗模式 + 主色） ----------------
    float y2 = cardC1.GetBottom() + 16 * s;
    Gdiplus::RectF cardC2(x, y2, w, 192 * s);
    FillRound(g, cardC2, 14 * s, pal.card);
    float cy2 = y2 + pad;
    SText(g, L"主题色", 18, FW_NORMAL, pal.title, light, x + pad, cy2, inner, 24 * s);
    cy2 += 26 * s;

    // 明暗模式：跟随系统 / 浅色 / 深色
    {
        const wchar_t* modes[3] = { L"跟随系统", L"浅色", L"深色" };
        const float segW = 216 * s, segH = 30 * s;
        SText(g, L"明暗模式", 15, FW_NORMAL, pal.title, light, x + pad, cy2,
            inner - segW - 12 * s, segH);
        Gdiplus::RectF seg(x + w - pad - segW, cy2, segW, segH);
        DrawSegmented(g, seg, modes, 3, ClampInt(g_cfg.lightMode, 0, 2), pal, light, ACT_LIGHT);
    }
    cy2 += 38 * s;

    // 跟随壁纸取色
    SText(g, L"跟随壁纸取色", 15, FW_NORMAL, pal.title, light, x + pad, cy2,
        inner - 60 * s, 22 * s);
    SText(g, L"从壁纸采样主色，换算成 oklch 的 hue", 13.5f, FW_NORMAL, sub, light,
        x + pad, cy2 + 22 * s, inner - 60 * s, 18 * s);
    {
        Gdiplus::RectF sw(x + w - pad - 44 * s, cy2 + 2 * s, 44 * s, 24 * s);
        DrawSwitch(g, sw, g_cfg.hueFollow != 0, pal);
        HitAdd((int)sw.X, (int)sw.Y, (int)sw.Width, (int)sw.Height, ACT_HUE_FOLLOW);
    }
    cy2 += 46 * s;

    {
        Gdiplus::RectF hue(x + pad, cy2, inner, 22 * s);
        DrawHueStrip(g, hue, (float)HueDeg(), pal);
        g_hueTrack = RECT{ (LONG)hue.X, (LONG)hue.Y, (LONG)hue.GetRight(), (LONG)hue.GetBottom() };
        HitAdd((int)hue.X - 4, (int)hue.Y - 6, (int)hue.Width + 8, (int)hue.Height + 12, ACT_HUE_STRIP);
    }
    cy2 += 28 * s;

    wchar_t hueText[64];
    WPrintf(hueText, 64, L"Hue %d · %s", HueDeg(),
        g_cfg.hueFollow ? L"取自壁纸" : L"手动");
    SText(g, hueText, 13.5f, FW_NORMAL, sub, light, x + pad, cy2, inner, 18 * s);

    // ---------------- 3. 面板（不透明度 / 圆角 / 显示位置） ----------------
    float y3 = cardC2.GetBottom() + 16 * s;
    Gdiplus::RectF cardC3(x, y3, w, 162 * s);   // 18 pad + 30 标题 + 68 行 + 28 行 + 18 pad
    FillRound(g, cardC3, 14 * s, pal.card);
    float cy3 = y3 + pad;
    SText(g, L"面板", 18, FW_NORMAL, pal.title, light, x + pad, cy3, inner, 24 * s);
    cy3 += 30 * s;

    struct { const wchar_t* name; int value; int act; int minV; int maxV; const wchar_t* unit; } rows[2] = {
        { L"卡片不透明度", g_cfg.opacity, ACT_OPACITY, 40, 100, L"%" },
        { L"圆角", g_cfg.radius, ACT_RADIUS, 0, 24, L"px" },
    };
    const float labelW = 110 * s;
    for (int i = 0; i < 2; i++) {
        float ry = cy3 + i * 34 * s;
        SText(g, rows[i].name, 15, FW_NORMAL, pal.title, light, x + pad, ry, labelW, 24 * s);
        Gdiplus::RectF track(x + pad + labelW, ry + 8 * s,
            inner - labelW - 64 * s, 9 * s);
        float t = (float)(rows[i].value - rows[i].minV) / (float)(rows[i].maxV - rows[i].minV);
        DrawSlider(g, track, t, pal, light);
        wchar_t val[32];
        WPrintf(val, 32, L"%d%s", rows[i].value, rows[i].unit);
        SText(g, val, 14, FW_NORMAL, sub, light, track.GetRight() + 8 * s, ry,
            56 * s, 24 * s, DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        RECT* target = (rows[i].act == ACT_OPACITY) ? &g_opTrack : &g_radTrack;
        *target = RECT{ (LONG)track.X, (LONG)track.Y, (LONG)track.GetRight(), (LONG)track.GetBottom() };
        HitAdd((int)track.X - 6, (int)ry, (int)track.Width + 12, (int)(26 * s), rows[i].act);
    }

    // 显示位置：右对齐的一组按钮（↑ ↓ ← → + 复位）
    {
        float ry = cy3 + 68 * s;
        SText(g, L"显示位置", 15, FW_NORMAL, pal.title, light, x + pad, ry, labelW, 28 * s);

        const float btnW = 30 * s, btnH = 28 * s, bGap = 5 * s, rstW = 54 * s;
        float right = x + w - pad;
        Gdiplus::RectF rst(right - rstW, ry, rstW, btnH);
        DrawTextButton(g, rst, L"复位", pal, light, g_setHot == ACT_POS + 4, ACT_POS + 4);

        float bx = rst.X - bGap - 4 * (btnW + bGap) + bGap;
        for (int i = 0; i < 4; i++) {
            Gdiplus::RectF b(bx + i * (btnW + bGap), ry, btnW, btnH);
            DrawArrowButton(g, b, i, pal, g_setHot == ACT_POS + i, ACT_POS + i);
        }

        wchar_t pos[48];
        WPrintf(pos, 48, L"横向 %+d · 纵向 %+d", (int)(g_cfg.posDx), (int)(g_cfg.posDy));
        SText(g, pos, 13.5f, FW_NORMAL, sub, light, x + pad + labelW, ry,
            bx - (x + pad + labelW) - 10 * s, 28 * s);
    }
}

static void PageContent(Gdiplus::Graphics& g, const Palette& pal, bool light,
    float x, float y, float w, float s, float availH)
{
    const wchar_t* names[TOGGLE_COUNT] = {
        L"处理器", L"主板与固件", L"内存", L"硬盘", L"显卡", L"网卡" };
    const wchar_t* metas[TOGGLE_COUNT] = {
        L"型号 / 核心 / 线程",
        L"厂商 / 固件版本 / 启动方式 / 证书",
        L"品牌 / 型号 / 容量 / 频率",
        L"型号 / 容量 / 分区格式 / 总线",
        L"型号（PE 下 PCI ID 兜底）",
        L"类型 / MAC / IP / 速率 / SSID / 网关" };

    // 行高在 84~104dp 之间自适应，正好铺满内容区，避免底部大片空白
    float rowH = (availH - 24 * s) / (float)TOGGLE_COUNT;
    if (rowH < 84 * s) rowH = 84 * s;
    if (rowH > 104 * s) rowH = 104 * s;

    const float cardH = TOGGLE_COUNT * rowH + 24 * s;
    float oy = y;
    if (cardH < availH - 1) {
        float off = (availH - cardH) * 0.5f;
        if (off > 40 * s) off = 40 * s;
        oy += off;
    }
    Gdiplus::RectF card(x, oy, w, cardH);
    FillRound(g, card, 14 * s, pal.card);

    for (int i = 0; i < TOGGLE_COUNT; i++) {
        float ry = oy + 12 * s + i * rowH;
        const float c = ry + rowH * 0.5f;   // 每行内容垂直居中
        if (i > 0) {
            Gdiplus::Pen line(pal.divider, 1.0f);
            g.DrawLine(&line, x + 20 * s, ry - 2 * s, card.GetRight() - 20 * s, ry - 2 * s);
        }

        // 84x48 preview of what the panel row will look like.
        Gdiplus::RectF prev(x + 20 * s, c - 24 * s, 84 * s, 48 * s);
        FillRound(g, prev, 9 * s, pal.chipBg);
        Gdiplus::Pen glyphPen(pal.chipFg, 1.4f);
        glyphPen.SetLineJoin(Gdiplus::LineJoinRound);
        glyphPen.SetStartCap(Gdiplus::LineCapRound);
        glyphPen.SetEndCap(Gdiplus::LineCapRound);
        DrawGlyph(g, glyphPen, i, prev.X + 8 * s, prev.Y + 12 * s, 24 * s, (float)s);
        FillRound(g, MRect(prev.X + 40 * s, prev.Y + 15 * s, 34 * s, 4 * s), 2 * s, pal.title);
        FillRound(g, MRect(prev.X + 40 * s, prev.Y + 25 * s, 30 * s, 3 * s), 1.5f * s, pal.detail);
        FillRound(g, MRect(prev.X + 40 * s, prev.Y + 33 * s, 24 * s, 3 * s), 1.5f * s, pal.detail);

        SText(g, names[i], 16, FW_NORMAL, pal.title, light,
            x + 122 * s, c - 24 * s, w - 122 * s - 84 * s, 24 * s);
        SText(g, metas[i], 13.5f, FW_NORMAL, pal.detail, light,
            x + 122 * s, c + 4 * s, w - 122 * s - 84 * s, 20 * s);

        Gdiplus::RectF sw(card.GetRight() - 20 * s - 44 * s, c - 12 * s, 44 * s, 24 * s);
        DrawSwitch(g, sw, ToggleValue(i) != 0, pal);
        HitAdd((int)(x + 10 * s), (int)(ry + 4 * s), (int)(w - 20 * s), (int)(rowH - 8 * s),
            ACT_TOGGLE + i);
    }
}

static void PageAbout(Gdiplus::Graphics& g, const Palette& pal, bool light,
    float x, float y, float w, float s, float availH)
{
    float cx = x + w * 0.5f;

    // 关于页内容高度固定，整体在内容区里垂直居中
    {
        const float blockH = 506 * s;
        float off = (availH - blockH) * 0.5f;
        if (off < 0) off = 0;
        y += off;
    }

    // App icon on a plate; the icon is a black outline, so invert it on dark.
    Gdiplus::RectF plate(cx - 38 * s, y + 6 * s, 76 * s, 76 * s);
    FillRound(g, plate, 18 * s, pal.card);
    Gdiplus::Bitmap* logo = light ? g_logoDark : g_logoLight;
    if (logo && logo->GetLastStatus() == Gdiplus::Ok) {
        float lz = 42 * s;
        g.DrawImage(logo, Gdiplus::RectF(cx - lz * 0.5f, y + 6 * s + (76 * s - lz) * 0.5f, lz, lz));
    }

    float ty = y + 96 * s;
    SText(g, L"轻谱 Hydrogen Spec", 24, FW_NORMAL, pal.title, light, x, ty, w, 34 * s,
        DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    wchar_t ver[96];
    WPrintf(ver, 96, L"v%s · %s · MIT License", kAppVersion,
        sizeof(void*) == 8 ? L"x64" : L"x86");
    SText(g, ver, 13.5f, FW_NORMAL, pal.detail, light, x, ty + 36 * s, w, 20 * s,
        DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SText(g, L"轻量、零依赖的桌面硬件信息面板", 14, FW_NORMAL, pal.detail, light,
        x, ty + 60 * s, w, 22 * s,
        DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

    float iy = ty + 96 * s;
    const float infoRow = 50 * s;
    Gdiplus::RectF info(x + 28 * s, iy, w - 56 * s, 4 * infoRow + 12 * s);
    FillRound(g, info, 14 * s, pal.card);
    const wchar_t* keys[4] = { L"许可", L"界面字体", L"数据来源", L"运行方式" };
    const wchar_t* vals[4] = {
        L"MIT License",
        L"MiSans Medium 子集 · 内嵌",
        L"SMBIOS · UEFI 变量 · 注册表 · 物理磁盘 IOCTL",
        L"单文件 · 静默 · 不联网" };
    for (int i = 0; i < 4; i++) {
        float ry = iy + 6 * s + i * infoRow;
        if (i > 0) {
            Gdiplus::Pen line(pal.divider, 1.0f);
            g.DrawLine(&line, info.X + 20 * s, ry, info.GetRight() - 20 * s, ry);
        }
        SText(g, keys[i], 14, FW_NORMAL, pal.detail, light, info.X + 20 * s, ry,
            110 * s, infoRow);
        SText(g, vals[i], 15, FW_NORMAL, pal.title, light, info.X + 136 * s, ry,
            info.Width - 156 * s, infoRow);
    }

    float by = info.GetBottom() + 20 * s;
    float bw1 = 156 * s, bw2 = 168 * s, bh = 44 * s;
    Gdiplus::RectF b1(cx - (bw1 + bw2 + 12 * s) * 0.5f, by, bw1, bh);
    Gdiplus::RectF b2(b1.GetRight() + 12 * s, by, bw2, bh);
    FillRound(g, b1, 11 * s, pal.chipBg);
    SText(g, L"GitHub 仓库", 15, FW_NORMAL, pal.itemFg, light, b1.X, b1.Y, b1.Width, b1.Height,
        DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    FillRound(g, b2, 11 * s, pal.primary);
    SText(g, L"复制版本信息", 15, FW_NORMAL, Gdiplus::Color(255, 255, 255, 255), true,
        b2.X, b2.Y, b2.Width, b2.Height,
        DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    HitAdd((int)b1.X, (int)b1.Y, (int)b1.Width, (int)b1.Height, ACT_GITHUB);
    HitAdd((int)b2.X, (int)b2.Y, (int)b2.Width, (int)b2.Height, ACT_COPYVER);

    SText(g, L"© 2026 Hydrogen Spec · 与轻键 / 轻韵同一套做法", 12, FW_NORMAL,
        pal.detail, light, x, by + bh + 20 * s, w, 18 * s,
        DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
}

// ---------------------------------------------------------------------------
// Frame + layered push
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// 主界面：硬件显示（Tab 1）
// ---------------------------------------------------------------------------

// Hardware pictograms on the same 24dp grid as the rail icons.
static void DrawHwIcon(Gdiplus::Graphics& g, int kind, float cx, float cy, float r,
    const Palette& pal)
{
    Gdiplus::SolidBrush bg(pal.chipBg);
    g.FillEllipse(&bg, cx - r, cy - r, r * 2, r * 2);

    const float u = r / 10.5f;
    Gdiplus::Pen pen(pal.chipFg, 1.7f * u);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);

    switch (kind) {
    case ICON_CPU: {
        Gdiplus::GraphicsPath sq;
        AddRoundRect(sq, Gdiplus::RectF(cx - 6.2f * u, cy - 6.2f * u, 12.4f * u, 12.4f * u),
            2.2f * u);
        g.DrawPath(&pen, &sq);
        Gdiplus::GraphicsPath in;
        AddRoundRect(in, Gdiplus::RectF(cx - 2.4f * u, cy - 2.4f * u, 4.8f * u, 4.8f * u),
            1.0f * u);
        g.DrawPath(&pen, &in);
        for (int i = -1; i <= 1; i += 2) {
            g.DrawLine(&pen, cx + i * 3.2f * u, cy - 8.2f * u, cx + i * 3.2f * u, cy - 6.6f * u);
            g.DrawLine(&pen, cx + i * 3.2f * u, cy + 6.6f * u, cx + i * 3.2f * u, cy + 8.2f * u);
            g.DrawLine(&pen, cx - 8.2f * u, cy + i * 3.2f * u, cx - 6.6f * u, cy + i * 3.2f * u);
            g.DrawLine(&pen, cx + 6.6f * u, cy + i * 3.2f * u, cx + 8.2f * u, cy + i * 3.2f * u);
        }
        break;
    }
    case ICON_BOARD: {
        Gdiplus::GraphicsPath bd;
        AddRoundRect(bd, Gdiplus::RectF(cx - 7.0f * u, cy - 6.4f * u, 14.0f * u, 12.8f * u),
            2.0f * u);
        g.DrawPath(&pen, &bd);
        Gdiplus::GraphicsPath chip;
        AddRoundRect(chip, Gdiplus::RectF(cx - 4.4f * u, cy - 3.8f * u, 4.6f * u, 4.6f * u),
            1.0f * u);
        g.DrawPath(&pen, &chip);
        g.DrawLine(&pen, cx + 1.6f * u, cy - 3.4f * u, cx + 4.6f * u, cy - 3.4f * u);
        g.DrawLine(&pen, cx + 1.6f * u, cy + 0.4f * u, cx + 4.6f * u, cy + 0.4f * u);
        g.DrawLine(&pen, cx - 4.4f * u, cy + 3.4f * u, cx + 4.6f * u, cy + 3.4f * u);
        break;
    }
    case ICON_RAM: {
        Gdiplus::GraphicsPath st;
        AddRoundRect(st, Gdiplus::RectF(cx - 4.2f * u, cy - 7.2f * u, 8.4f * u, 14.4f * u),
            1.8f * u);
        g.DrawPath(&pen, &st);
        for (int i = -1; i <= 1; i++)
            g.DrawLine(&pen, cx + i * 2.2f * u, cy - 3.6f * u, cx + i * 2.2f * u, cy + 3.0f * u);
        g.DrawLine(&pen, cx - 2.8f * u, cy + 5.2f * u, cx + 2.8f * u, cy + 5.2f * u);
        break;
    }
    case ICON_DISK: {
        Gdiplus::GraphicsPath dk;
        AddRoundRect(dk, Gdiplus::RectF(cx - 6.6f * u, cy - 6.6f * u, 13.2f * u, 13.2f * u),
            2.2f * u);
        g.DrawPath(&pen, &dk);
        Gdiplus::SolidBrush dot(pal.chipFg);
        g.FillEllipse(&dot, cx - 4.2f * u, cy - 4.2f * u, 2.6f * u, 2.6f * u);
        g.DrawLine(&pen, cx - 4.4f * u, cy + 3.6f * u, cx + 4.4f * u, cy + 3.6f * u);
        break;
    }
    case ICON_GPU: {
        Gdiplus::GraphicsPath gd;
        AddRoundRect(gd, Gdiplus::RectF(cx - 7.2f * u, cy - 5.0f * u, 14.4f * u, 10.0f * u),
            2.0f * u);
        g.DrawPath(&pen, &gd);
        g.DrawEllipse(&pen, cx - 2.0f * u, cy - 2.6f * u, 5.2f * u, 5.2f * u);
        g.DrawLine(&pen, cx - 4.6f * u, cy + 6.4f * u, cx - 4.6f * u, cy + 8.0f * u);
        g.DrawLine(&pen, cx + 3.4f * u, cy + 6.4f * u, cx + 3.4f * u, cy + 8.0f * u);
        break;
    }
    default: {   // ICON_NET: Wi-Fi
        for (int i = 0; i < 3; i++) {
            float rr = (2.6f + i * 2.6f) * u;
            g.DrawArc(&pen, cx - rr, cy + 3.0f * u - rr, rr * 2, rr * 2, 200.0f, 140.0f);
        }
        Gdiplus::SolidBrush dot(pal.chipFg);
        g.FillEllipse(&dot, cx - 1.3f * u, cy + 2.2f * u, 2.6f * u, 2.6f * u);
        break;
    }
    }
}

static void PageHardware(Gdiplus::Graphics& g, const Palette& pal, bool light,
    float x, float y, float w, float s, float availH)
{
    if (g_rowCount <= 0) return;

    const float gapBase = 16 * s;
    const float cardW = (w - gapBase) * 0.5f;
    const int perCol = (g_rowCount + 1) / 2;

    // 自然高度（标题 26 + 每条明细 22 + 上下留白），再按剩余空间等比撑开，
    // 免得 6 张卡只占上半屏、下面一大片空白。
    auto natural = [&](int i) { return 88 * s + g_rows[i].detailCount * 22 * s; };

    int colOf[16] = { 0 };
    float colH[2] = { 0, 0 };
    for (int i = 0; i < g_rowCount && i < 16; i++) {
        int col = (colH[0] <= colH[1]) ? 0 : 1;
        colOf[i] = col;
        colH[col] += natural(i) + gapBase;
    }
    float maxNat = 0;
    for (int c = 0; c < 2; c++) {
        colH[c] -= gapBase;
        if (colH[c] > maxNat) maxNat = colH[c];
    }

    float extra = 0, gap = gapBase, offsetY = 0;
    if (availH > maxNat + 1) {
        float room = availH - maxNat;
        extra = room / (float)perCol;
        if (extra > 30 * s) extra = 30 * s;
        room -= extra * perCol;
        if (perCol > 1) {
            float grow = room / (float)(perCol - 1);   // 余量并进行间距，正好铺满
            if (grow > 26 * s) grow = 26 * s;
            gap += grow;
            room -= grow * (perCol - 1);
        }
        offsetY = room * 0.5f;
        if (offsetY > 20 * s) offsetY = 20 * s;
    }

    const float lineH = 22 * s;
    float cy[2] = { y + offsetY, y + offsetY };
    for (int i = 0; i < g_rowCount; i++) {
        const int col = colOf[i];
        const float cardH = natural(i) + extra;
        Gdiplus::RectF rc(x + col * (cardW + gapBase), cy[col], cardW, cardH);
        cy[col] += cardH + gap;
        FillRound(g, rc, 14 * s, pal.card);

        // 内容整体垂直居中，撑高之后不会全堆在卡片顶部
        const float titleH = 28 * s;
        const float contentH = titleH + g_rows[i].detailCount * lineH;
        const float top = rc.Y + (cardH - contentH) * 0.5f;

        DrawHwIcon(g, g_rows[i].icon, rc.X + 36 * s, top + titleH * 0.5f, 15 * s, pal);
        SText(g, g_rows[i].title, 15, FW_NORMAL, pal.title, light,
            rc.X + 60 * s, top, cardW - 74 * s, titleH);
        for (int d = 0; d < g_rows[i].detailCount; d++) {
            SText(g, g_rows[i].details[d], 13, FW_NORMAL, pal.detail, light,
                rc.X + 60 * s, top + titleH + d * lineH, cardW - 74 * s, lineH);
        }
    }
}

// ---------------------------------------------------------------------------
// 系统信息页
// ---------------------------------------------------------------------------

static TargetInfo g_ti;
static bool g_tiReady = false;
static wchar_t g_saveDir[MAX_PATH] = { 0 };

static void EnsureSaveDir(void)
{
    if (g_saveDir[0]) return;
    wchar_t docs[MAX_PATH] = { 0 };
    KnownFolderPath(FOLDERID_Documents, docs, MAX_PATH);
    if (!docs[0]) return;
    WPrintf(g_saveDir, MAX_PATH, L"%s\\HSpec", docs);
    CreateDirectoryW(g_saveDir, NULL);
}

static void GetLocalIpText(wchar_t* out, int cch)
{
    if (!out || cch <= 0) return;
    out[0] = 0;
    ULONG size = 0;
    const DWORD flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    if (GetAdaptersAddresses(AF_INET, flags, NULL, NULL, &size) != ERROR_BUFFER_OVERFLOW || !size)
        return;
    IP_ADAPTER_ADDRESSES* addrs = (IP_ADAPTER_ADDRESSES*)malloc(size);
    if (!addrs) return;
    if (GetAdaptersAddresses(AF_INET, flags, NULL, addrs, &size) == NO_ERROR) {
        for (IP_ADAPTER_ADDRESSES* a = addrs; a && !out[0]; a = a->Next) {
            if (a->OperStatus != IfOperStatusUp) continue;
            if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            for (IP_ADAPTER_UNICAST_ADDRESS* u = a->FirstUnicastAddress; u; u = u->Next) {
                if (!u->Address.lpSockaddr) continue;
                if (u->Address.lpSockaddr->sa_family != AF_INET) continue;
                wchar_t ip[64] = { 0 };
                FormatIpv4(u->Address.lpSockaddr, ip, 64);
                if (ip[0]) {
                    wcsncpy_s(out, cch, ip, _TRUNCATE);
                    break;
                }
            }
        }
    }
    free(addrs);
}

static void PageSystem(Gdiplus::Graphics& g, const Palette& pal, bool light,
    float x, float y, float w, float s, float availH)
{
    if (!g_tiReady) {
        CollectTargetInfo(&g_ti);
        g_tiReady = true;
    }
    const TargetInfo& t = g_ti;
    const float keyW = 78 * s;
    const float pad = 18 * s;

    // ---------------- 厂商卡 ----------------

    // 行高按可用高度自适应（23~30dp）：两张卡共用同一行高，
    // 卡一 5 行、卡二 11 行，剩下的空间上下对半分，避免上紧下松。
    const float btnH = 40 * s;
    const float gapCard = 12 * s, gapBtn = 14 * s;
    float rowH = (availH - gapCard - gapBtn - btnH - pad * 4) / 16.0f;
    if (rowH < 23 * s) rowH = 23 * s;
    if (rowH > 30 * s) rowH = 30 * s;

    const float c1h = 5 * rowH + pad * 2;
    const float c1hh = c1h;   // 兼容下方引用
    const float c2h = 11 * rowH + pad * 2;
    {
        float blockH = c1hh + gapCard + c2h + gapBtn + btnH;
        float offset = (availH - blockH) * 0.5f;
        if (offset < 0) offset = 0;
        if (offset > 40 * s) offset = 40 * s;
        y += offset;
    }
    FillRound(g, MRect(x, y, w, c1h), 16 * s, pal.card);

    Gdiplus::RectF slot(x + pad, y + pad, 116 * s, c1h - pad * 2);
    FillRound(g, slot, 12 * s, pal.chipBg);
    bool drew = false;
    if (t.logoFile[0]) {
        Gdiplus::Bitmap bmp(t.logoFile);
        if (bmp.GetLastStatus() == Gdiplus::Ok && bmp.GetWidth() > 0 && bmp.GetHeight() > 0) {
            float iw = (float)bmp.GetWidth(), ih = (float)bmp.GetHeight();
            float maxW = slot.Width - 18 * s, maxH = slot.Height - 18 * s;
            float k = maxW / iw;
            if (ih * k > maxH) k = maxH / ih;
            float dw = iw * k, dh = ih * k;
            g.DrawImage(&bmp, Gdiplus::RectF(slot.X + (slot.Width - dw) * 0.5f,
                slot.Y + (slot.Height - dh) * 0.5f, dw, dh));
            drew = true;
        }
    }
    if (!drew) {
        SText(g, t.manufacturer[0] ? t.manufacturer : L"OEM", 15, FW_NORMAL, pal.detail, light,
            slot.X + 6 * s, slot.Y, slot.Width - 12 * s, slot.Height,
            DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    {
        float kx = slot.GetRight() + 16 * s;
        float vw = x + w - pad - (kx + keyW + 10 * s);
        struct KV { const wchar_t* k; const wchar_t* v; };
        const KV rows[5] = {
            { L"制造商", t.manufacturer },
            { L"型号", t.model },
            { L"电话号码", t.phone },
            { L"网址", t.supportUrl },
            { L"Logo位置", t.logoFile },
        };
        float ky = y + pad;
        for (int i = 0; i < 5; i++) {
            SText(g, rows[i].k, 13.5f, FW_NORMAL, pal.detail, light,
                kx, ky + i * rowH, keyW, rowH);
            SText(g, rows[i].v, 13.5f, FW_NORMAL, pal.title, light,
                kx + keyW + 10 * s, ky + i * rowH, vw, rowH);
        }
    }

    // ---------------- 两列信息卡 ----------------
    const float c2y = y + c1h + gapCard;
    FillRound(g, MRect(x, c2y, w, c2h), 16 * s, pal.card);

    struct KV2 { const wchar_t* k; const wchar_t* v; };
    const KV2 leftCol[9] = {
        { L"系统盘符", t.systemDrive },
        { L"用户名称", t.userName },
        { L"桌面路径", t.desktop },
        { L"下载路径", t.downloads },
        { L"收藏夹", t.favorites },
        { L"文档路径", t.documents },
        { L"图片路径", t.pictures },
        { L"音乐路径", t.music },
        { L"视频路径", t.videos },
    };
    const KV2 rightCol[11] = {
        { L"系统名称", t.osName },
        { L"计算机名", t.computerName },
        { L"版本类别", t.edition },
        { L"系统位数", t.arch },
        { L"版本内核", t.kernel },
        { L"发行号", t.releaseId },
        { L"版本编号", t.build },
        { L"补丁号", t.ubr },
        { L"实验版本", t.lab },
        { L"安装日期", t.installDate },
        { L"注册用户", t.regUser },
    };

    const float inner = w - pad * 2;
    const float colGap = 18 * s;
    const float leftW = inner * 0.46f - colGap * 0.5f;
    const float rightX = x + pad + leftW + colGap;
    const float rightW = inner - leftW - colGap;
    const float lk = 66 * s;
    const float rk = 76 * s;

    for (int i = 0; i < 9; i++) {
        float ry = c2y + pad + i * rowH;
        SText(g, leftCol[i].k, 13, FW_NORMAL, pal.detail, light, x + pad, ry, lk, rowH);
        SText(g, leftCol[i].v, 13, FW_NORMAL, pal.title, light, x + pad + lk + 4 * s, ry,
            leftW - lk - 4 * s, rowH);
    }
    for (int i = 0; i < 11; i++) {
        float ry = c2y + pad + i * rowH;
        SText(g, rightCol[i].k, 13, FW_NORMAL, pal.detail, light, rightX, ry, rk, rowH);
        SText(g, rightCol[i].v, 13, FW_NORMAL, pal.title, light, rightX + rk, ry,
            rightW - rk, rowH);
    }

    // ---------------- 按钮行 ----------------
    const float btnY = c2y + c2h + gapBtn;
    const float gap = 10 * s;
    const float btnW = (w - gap * 4) / 5.0f;
    const wchar_t* labels[5] = { L"IP 地址查看", L"保存所有信息", L"打开保存目录",
                                 L"使用说明", L"返回上级" };
    const int acts[5] = { ACT_SHOWIP, ACT_SAVEINFO, ACT_OPENFOLDER, ACT_HELP, ACT_BACK };
    for (int i = 0; i < 5; i++) {
        Gdiplus::RectF b(x + i * (btnW + gap), btnY, btnW, btnH);
        DrawTextButton(g, b, labels[i], pal, light, g_setHot == acts[i], acts[i]);
    }
}

static void SettingsDraw(Gdiplus::Graphics& g)
{
    double s = SettingsScale();
    // 明暗模式：浅色/深色强制，跟随则用壁纸明暗推断（和面板一致）
    const bool darkPage = (g_cfg.lightMode == 1) ? false
                        : (g_cfg.lightMode == 2) ? true
                        : g_lightText;
    Palette pal = MakePalette(darkPage, 255);
    const bool light = pal.darkCard;   // light text == dark page

    Gdiplus::RectF page(0, 0, (float)g_setW, (float)g_setH);
    g.Clear(pal.page);

    float px = 24 * (float)s;
    float py = 14 * (float)s;

    // ---- title bar (drag region; only the close button is interactive) ----
    {
        Gdiplus::Bitmap* logo = light ? g_logoDark : g_logoLight;
        if (logo && logo->GetLastStatus() == Gdiplus::Ok)
            g.DrawImage(logo, Gdiplus::RectF(px, py + 4 * s, 22 * s, 22 * s));
        SText(g, g_setMode ? L"轻谱 · 设置" : L"轻谱", 14, FW_NORMAL, pal.detail, light,
            px + 30 * s, py, 240 * s, 30 * s);

        // 关闭按钮：沿用 HKeyBoard 标题栏的叉号规格——12 格网、叉面 10.5dp、
        // 笔画 1.3 格（≈2.2dp）、圆头端点，视觉比细长线更"实"。
        Gdiplus::RectF close(page.GetRight() - 62 * s, py + 1 * s, 34 * s, 30 * s);
        if (g_setHot == ACT_CLOSE) FillRound(g, close, 6 * s, pal.chipBg);
        Gdiplus::Pen cp(pal.detail, 2.2f * (float)s);
        cp.SetStartCap(Gdiplus::LineCapRound);
        cp.SetEndCap(Gdiplus::LineCapRound);
        float cxm = close.X + close.Width * 0.5f, cym = close.Y + close.Height * 0.5f;
        float d5 = 5.25f * (float)s;
        g.DrawLine(&cp, cxm - d5, cym - d5, cxm + d5, cym + d5);
        g.DrawLine(&cp, cxm + d5, cym - d5, cxm - d5, cym + d5);
        HitAdd((int)close.X, (int)close.Y, (int)close.Width, (int)close.Height, ACT_CLOSE);
    }

    const float navX = 20 * (float)s;
    const float navY = 76 * (float)s;

    // ---- sidebar ----
    if (g_setMode == 0) {
        // 主界面：硬件显示 / 目标系统信息
        const wchar_t* names[2] = { L"硬件显示", L"目标系统信息" };
        for (int i = 0; i < 2; i++) {
            Gdiplus::RectF item(navX, navY + i * 54 * s, 200 * s, 46 * s);
            bool on = (g_mainTab == i);
            if (on) FillRound(g, item, 12 * s, pal.itemBg);
            Gdiplus::Color fg = on ? pal.itemFg : pal.detail;
            DrawNavIcon(g, i == 0 ? 3 : 4, item.X + 14 * s, item.Y + 13 * s, 20 * s, fg);
            SText(g, names[i], 15, FW_NORMAL, fg, light,
                item.X + 46 * s, item.Y, item.Width - 56 * s, item.Height);
            HitAdd((int)item.X, (int)item.Y, (int)item.Width, (int)item.Height, ACT_NAV + i);
        }

        // 设置：二级页面入口
        Gdiplus::RectF setItem(navX, page.GetBottom() - 118 * s, 200 * s, 46 * s);
        if (g_setHot == ACT_OPEN_SET) FillRound(g, setItem, 12 * s, pal.chipBg);
        DrawNavIcon(g, 5, setItem.X + 14 * s, setItem.Y + 13 * s, 20 * s, pal.detail);
        SText(g, L"设置", 15, FW_NORMAL, pal.detail, light,
            setItem.X + 46 * s, setItem.Y, setItem.Width - 56 * s, setItem.Height);
        HitAdd((int)setItem.X, (int)setItem.Y, (int)setItem.Width, (int)setItem.Height,
            ACT_OPEN_SET);
    } else {
        // 二级：返回 + 设置分页
        Gdiplus::RectF back(navX, navY, 200 * s, 46 * s);
        if (g_setHot == ACT_BACK) FillRound(g, back, 12 * s, pal.chipBg);
        {
            Gdiplus::Pen ap(pal.detail, 1.8f);
            ap.SetStartCap(Gdiplus::LineCapRound);
            ap.SetEndCap(Gdiplus::LineCapRound);
            float ax = back.X + 20 * s, ay = back.Y + back.Height * 0.5f;
            g.DrawLine(&ap, ax + 8 * s, ay - 6 * s, ax, ay);
            g.DrawLine(&ap, ax, ay, ax + 8 * s, ay + 6 * s);
        }
        SText(g, L"返回", 15, FW_NORMAL, pal.detail, light,
            back.X + 46 * s, back.Y, back.Width - 56 * s, back.Height);
        HitAdd((int)back.X, (int)back.Y, (int)back.Width, (int)back.Height, ACT_BACK);

        const wchar_t* names[3] = { L"外观", L"显示内容", L"关于" };
        for (int i = 0; i < 3; i++) {
            Gdiplus::RectF item(navX, navY + 54 * s + i * 54 * s, 200 * s, 46 * s);
            bool on = (g_setPage == i);
            if (on) FillRound(g, item, 12 * s, pal.itemBg);
            Gdiplus::Color fg = on ? pal.itemFg : pal.detail;
            DrawNavIcon(g, i == 2 ? 2 : i, item.X + 14 * s, item.Y + 13 * s, 20 * s, fg);
            SText(g, names[i], 15, FW_NORMAL, fg, light,
                item.X + 46 * s, item.Y, item.Width - 56 * s, item.Height);
            HitAdd((int)item.X, (int)item.Y, (int)item.Width, (int)item.Height,
                ACT_SETNAV + i);
        }
    }

    wchar_t foot[64];
    WPrintf(foot, 64, L"HSpec %s", kAppVersion);
    SText(g, foot, 12, FW_NORMAL, pal.detail, light, navX + 6 * s,
        page.GetBottom() - 44 * s, 160 * s, 18 * s);

    // ---- content ----
    const float cxp = 240 * (float)s;
    float cyp = 24 * (float)s;
    const float cwp = page.Width - 240 * s - 32 * s;

    const wchar_t* title = L"";
    const wchar_t* subtitle = L"";
    if (g_setMode == 0) {
        if (g_mainTab == 0) {
            title = L"硬件显示";
            subtitle = L"这台机器的处理器、主板、内存、显卡与存储";
        } else {
            title = L"目标系统信息";
            subtitle = L"厂商、系统版本与用户目录，可一键保存成资料";
        }
    } else if (g_setPage == 0) {
        title = L"外观";
        subtitle = L"面板画在桌面层上，这里决定它离壁纸有多近";
    } else if (g_setPage == 1) {
        title = L"显示内容";
        subtitle = L"关掉的项目不再出现在面板上";
    }

    if (title[0]) {
        FillRound(g, MRect(cxp, cyp + 6 * s, 4 * s, 24 * s), 2 * s, pal.primary);
        SText(g, title, 24, FW_NORMAL, pal.title, light, cxp + 16 * s, cyp,
            cwp - 16 * s, 32 * s);
        SText(g, subtitle, 14, FW_NORMAL, pal.detail, light, cxp + 16 * s, cyp + 34 * s,
            cwp - 16 * s, 20 * s);
        cyp += 68 * s;               // 标题块 44 + 与内容之间的 24 间隔
    }

    const float availH = (page.Height - 24 * (float)s) - cyp;   // 统一底部留白 24dp

    if (g_setMode == 0) {
        if (g_mainTab == 0) PageHardware(g, pal, light, cxp, cyp, cwp, (float)s, availH);
        else PageSystem(g, pal, light, cxp, cyp, cwp, (float)s, availH);
    } else {
        if (g_setPage == 0) PageLook(g, pal, light, cxp, cyp, cwp, (float)s);
        else if (g_setPage == 1) PageContent(g, pal, light, cxp, cyp, cwp, (float)s, availH);
        else PageAbout(g, pal, light, cxp, cyp, cwp, (float)s, availH);
    }
}

static void SettingsPaint(Gdiplus::Graphics& g)
{
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    g.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    g.Clear(Gdiplus::Color(0, 0, 0, 0));
    g_hitCount = 0;
    SettingsDraw(g);
}

// Off-screen render, used by --dump-settings so the window can be inspected
// without a desktop session.
static bool SettingsSavePng(const wchar_t* path)
{
    CLSID png;
    if (GetEncoderClsid(L"image/png", &png) < 0) return false;
    Gdiplus::Bitmap bmp(g_setW, g_setH, PixelFormat32bppPARGB);
    if (bmp.GetLastStatus() != Gdiplus::Ok) return false;
    {
        Gdiplus::Graphics g(&bmp);
        SettingsPaint(g);
    }
    return bmp.Save(path, &png, NULL) == Gdiplus::Ok;
}

// The settings window is a normal (non-layered) window painted on WM_PAINT;
// "pushing" just invalidates.
static bool SettingsPush(void)
{
    if (!g_setHwnd) return false;
    InvalidateRect(g_setHwnd, NULL, FALSE);
    return true;
}

static void SettingsInvalidate(void)
{
    SettingsPush();
}

// ---------------------------------------------------------------------------
// Interaction
// ---------------------------------------------------------------------------

static void CopyVersionToClipboard(void)
{
    wchar_t text[160];
    WPrintf(text, 160, L"HSpec 轻谱 v%s (%s, %s)", kAppVersion,
        sizeof(void*) == 8 ? L"x64" : L"x86", L"MIT");
    if (!OpenClipboard(g_setHwnd)) return;
    EmptyClipboard();
    size_t bytes = (wcslen(text) + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (mem) {
        void* dst = GlobalLock(mem);
        if (dst) {
            memcpy(dst, text, bytes);
            GlobalUnlock(mem);
            if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
        } else {
            GlobalFree(mem);
        }
    }
    CloseClipboard();
}

static void SetSliderFromX(int act, int x)
{
    RECT* tr = (act == ACT_OPACITY) ? &g_opTrack : (act == ACT_RADIUS ? &g_radTrack : &g_hueTrack);
    int w = tr->right - tr->left;
    if (w <= 0) return;
    float t = (float)(x - tr->left) / (float)w;
    if (t < 0) t = 0;
    if (t > 1) t = 1;

    if (act == ACT_OPACITY) {
        int v = 40 + (int)(t * 60.0f + 0.5f);
        if (v != g_cfg.opacity) { g_cfg.opacity = v; PanelPreview(); }
    } else if (act == ACT_RADIUS) {
        int v = (int)(t * 24.0f + 0.5f);
        if (v != g_cfg.radius) { g_cfg.radius = v; PanelPreview(); }
    } else if (act == ACT_HUE_STRIP) {
        int v = (int)(t * 360.0f + 0.5f);
        g_cfg.hueFollow = 0;
        if (v != g_cfg.hue) { g_cfg.hue = v; PanelPreview(); }
    }
}

static void PerformAction(int act)
{
    if (act >= ACT_NAV && act < ACT_NAV + 2) {
        g_mainTab = act - ACT_NAV;          // 主界面：硬件显示 / 目标系统信息
    } else if (act >= ACT_SETNAV && act < ACT_SETNAV + 3) {
        g_setPage = act - ACT_SETNAV;       // 设置内：外观 / 显示内容 / 关于
    } else if (act == ACT_OPEN_SET) {
        g_setMode = 1;                      // 进入设置（二级）
    } else if (act >= ACT_THEME && act < ACT_THEME + 3) {
        int t = act - ACT_THEME;
        if (t != g_cfg.theme) { g_cfg.theme = t; PanelReload(false); }
    } else if (act >= ACT_TOGGLE && act < ACT_TOGGLE + TOGGLE_COUNT) {
        ToggleFlip(act - ACT_TOGGLE);
        PanelReload(true);
    } else if (act == ACT_HUE_FOLLOW) {
        g_cfg.hueFollow = !g_cfg.hueFollow;
        PanelReload(false);
    } else if (act >= ACT_LIGHT && act < ACT_LIGHT + 3) {
        g_cfg.lightMode = act - ACT_LIGHT;
        PanelReload(false);
    } else if (act >= ACT_SWATCH && act < ACT_SWATCH + 6) {
        int idx = act - ACT_SWATCH;
        if (idx == 0) {
            g_cfg.hueFollow = 1;
        } else {
            static const int kPreset[5] = { 250, 190, 160, 300, 20 };
            g_cfg.hueFollow = 0;
            g_cfg.hue = kPreset[idx - 1];
        }
        PanelReload(false);
    } else if (act >= ACT_POS && act < ACT_POS + 5) {
        int step = 8;
        switch (act - ACT_POS) {
        case 0: g_cfg.posDy -= step; break;
        case 1: g_cfg.posDy += step; break;
        case 2: g_cfg.posDx -= step; break;
        case 3: g_cfg.posDx += step; break;
        default: g_cfg.posDx = g_cfg.posDy = 0; break;
        }
        g_cfg.posDx = ClampInt(g_cfg.posDx, -400, 400);
        g_cfg.posDy = ClampInt(g_cfg.posDy, -400, 400);
        PanelReload(false);
    } else if (act == ACT_CLOSE) {
        PostMessageW(g_setHwnd, WM_CLOSE, 0, 0);
        return;
    } else if (act == ACT_BACK) {
        if (g_setMode) g_setMode = 0;       // 设置 → 回主界面
        else g_mainTab = 0;                 // 系统信息页 → 回硬件显示
        g_setPage = 0;
    } else if (act == ACT_SAVEINFO) {
        if (!g_tiReady) { CollectTargetInfo(&g_ti); g_tiReady = true; }
        EnsureSaveDir();
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t path[MAX_PATH] = { 0 };
        WPrintf(path, MAX_PATH, L"%s\\系统信息_%04d%02d%02d_%02d%02d%02d.txt",
            g_saveDir, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        if (SaveTargetInfo(path, &g_ti))
            MessageBoxW(g_setHwnd, path, L"已保存", MB_ICONINFORMATION | MB_OK);
        else
            MessageBoxW(g_setHwnd, L"保存失败，请检查「文档」目录的写入权限。",
                L"HSpec", MB_ICONWARNING | MB_OK);
    } else if (act == ACT_OPENFOLDER) {
        EnsureSaveDir();
        if (g_saveDir[0])
            ShellExecuteW(NULL, L"open", g_saveDir, NULL, NULL, SW_SHOWNORMAL);
    } else if (act == ACT_SHOWIP) {
        wchar_t ip[64] = { 0 };
        GetLocalIpText(ip, 64);
        wchar_t msg[320] = { 0 };
        WPrintf(msg, 320, L"本机 IPv4 地址：\n\n%s",
            ip[0] ? ip : L"未检测到已连接的网络");
        MessageBoxW(g_setHwnd, msg, L"IP 地址查看", MB_ICONINFORMATION | MB_OK);
    } else if (act == ACT_HELP) {
        MessageBoxW(g_setHwnd,
            L"系统信息页汇总本机的厂商、系统版本与用户目录。\n\n"
            L"· 保存所有信息：导出到「文档\\HSpec」，文件名带时间戳；\n"
            L"· 打开保存目录：在资源管理器中打开该目录；\n"
            L"· IP 地址查看：显示当前联网网卡的 IPv4 地址；\n"
            L"· 返回上级：回到外观页。",
            L"使用说明", MB_ICONINFORMATION | MB_OK);
    } else if (act == ACT_GITHUB) {
        ShellExecuteW(NULL, L"open", L"https://github.com/PanDaTech/Hydrogen-Spec", NULL, NULL,
            SW_SHOWNORMAL);
    } else if (act == ACT_COPYVER) {
        CopyVersionToClipboard();
    }
    SettingsInvalidate();
}

static LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        if (g_setDrag) {
            SetSliderFromX(g_setDrag, x);
            SettingsInvalidate();
            return 0;
        }
        int act = HitTest(x, y);
        if (act != g_setHot) {
            g_setHot = act;
            SettingsInvalidate();
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        int act = HitTest(x, y);
        if (act == ACT_OPACITY || act == ACT_RADIUS || act == ACT_HUE_STRIP) {
            g_setDrag = act;
            SetCapture(hwnd);
            SetSliderFromX(act, x);
            SettingsInvalidate();
        } else if (act != ACT_NONE) {
            PerformAction(act);
        }
        return 0;
    }
    case WM_LBUTTONUP:
        if (g_setDrag) {
            g_setDrag = 0;
            ReleaseCapture();
            PanelCommit();
        }
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        if (wParam == VK_TAB) {
            g_setPage = (g_setPage + ((GetKeyState(VK_SHIFT) & 0x8000) ? 2 : 1)) % 3;
            SettingsInvalidate();
            return 0;
        }
        if (wParam == VK_LEFT || wParam == VK_RIGHT) {
            int step = (wParam == VK_RIGHT) ? 1 : -1;
            g_cfg.hue = (g_cfg.hue + step * 5 + 360) % 360;
            g_cfg.hueFollow = 0;
            PanelPreview();
            PanelCommit();
            SettingsInvalidate();
            return 0;
        }
        return 0;

    case WM_SETCURSOR: {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hwnd, &pt);
        int act = HitTest(pt.x, pt.y);
        if (act != ACT_NONE) SetCursor(LoadCursorW(NULL, IDC_HAND));
        else SetCursor(LoadCursorW(NULL, IDC_ARROW));
        return TRUE;
    }

    case WM_NCCALCSIZE:
        // Borderless: the client area is the whole window.  With WS_THICKFRAME
        // this also kills the invisible resize frame (our WM_NCHITTEST below
        // never returns a resize code either), keeping the size fixed.
        return 0;

    case WM_NCACTIVATE:
        // DefWindowProc repaints the WS_THICKFRAME non-client border whenever
        // the activation state changes - that is the gray frame that popped up
        // around the window on every window switch.  The documented custom-frame
        // trick: pass -1 as lParam so the activation bookkeeping still happens
        // but the frame paint is skipped.
        return DefWindowProcW(hwnd, msg, wParam, (LPARAM)-1);

    case WM_NCPAINT:
        return 0;

    case WM_NCHITTEST: {
        // Interactive controls get the client; everything else is the drag
        // region - the window is borderless, so this is how it moves.
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &pt);
        if (HitTest(pt.x, pt.y) != ACT_NONE) return HTCLIENT;
        return HTCAPTION;
    }

    case WM_DPICHANGED: {
        RECT* target = (RECT*)lParam;
        UINT ndpi = (UINT)HIWORD(wParam);
        if (ndpi == 0) ndpi = (UINT)LOWORD(wParam);
        HMONITOR mon = MonitorFromRect(target, MONITOR_DEFAULTTONEAREST);
        SettingsLayoutFor(mon, ndpi);
        SetWindowPos(hwnd, NULL, target->left, target->top, g_setW, g_setH,
            SWP_NOZORDER | SWP_NOACTIVATE);
        SettingsInvalidate();
        return 0;
    }

    case WM_DISPLAYCHANGE: {
        // Resolution changed - refit so the window never outgrows the screen.
        HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        SettingsLayoutFor(mon, 0);
        SetWindowPos(hwnd, NULL, 0, 0, g_setW, g_setH,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        SettingsInvalidate();
        return 0;
    }

    case WM_SETTINGCHANGE: {
        HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        SettingsLayoutFor(mon, 0);
        SetWindowPos(hwnd, NULL, 0, 0, g_setW, g_setH,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        SettingsInvalidate();
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;
        if (w > 0 && h > 0) {
            HDC mem = CreateCompatibleDC(dc);
            BITMAPINFO bi;
            memset(&bi, 0, sizeof(bi));
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = w;
            bi.bmiHeader.biHeight = -h;
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            void* bits = NULL;
            HBITMAP hbm = mem ? CreateDIBSection(mem, &bi, DIB_RGB_COLORS,
                &bits, NULL, 0) : NULL;
            if (hbm && bits) {
                HBITMAP old = (HBITMAP)SelectObject(mem, hbm);
                {
                    Gdiplus::Graphics g(mem);
                    SettingsPaint(g);
                }
                BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
                SelectObject(mem, old);
            }
            if (hbm) DeleteObject(hbm);
            if (mem) DeleteDC(mem);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        g_setHwnd = NULL;
        g_setDrag = 0;
        g_setHot = 0;
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Open
// ---------------------------------------------------------------------------

static void LoadLogoBitmaps(void)
{
    if (g_logoLight) return;

    const int size = 64;
    HICON icon = (HICON)LoadImageW(GetModuleHandleW(NULL),
        MAKEINTRESOURCEW(IDI_MAIN_ICON), IMAGE_ICON, size, size, LR_DEFAULTCOLOR);
    if (!icon) return;

    // Bitmap::FromHICON() loses the alpha mask (a black square on a light page),
    // so composite the icon into a 32bpp DIB ourselves.
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = -size;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = NULL;
    HDC screen = GetDC(NULL);
    HBITMAP hbm = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP old = (HBITMAP)SelectObject(dc, hbm);
    if (bits) memset(bits, 0, (size_t)size * size * 4);
    DrawIconEx(dc, 0, 0, icon, size, size, 0, NULL, DI_NORMAL);
    SelectObject(dc, old);
    DeleteDC(dc);
    DestroyIcon(icon);

    if (hbm && bits) {
        // Wrap the DIB only long enough to copy it out: the memory dies with hbm.
        Gdiplus::Bitmap wrap(size, size, size * 4, PixelFormat32bppPARGB, (BYTE*)bits);
        g_logoLight = new Gdiplus::Bitmap(size, size, PixelFormat32bppPARGB);
        g_logoDark = new Gdiplus::Bitmap(size, size, PixelFormat32bppPARGB);
        if (g_logoLight->GetLastStatus() == Gdiplus::Ok) {
            Gdiplus::Graphics lg(g_logoLight);
            lg.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
            lg.DrawImage(&wrap, 0, 0, size, size);
        }
        if (g_logoDark->GetLastStatus() == Gdiplus::Ok) {
            Gdiplus::Graphics dg(g_logoDark);
            dg.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
            dg.DrawImage(&wrap, 0, 0, size, size);
        }
    }
    if (hbm) DeleteObject(hbm);
    ReleaseDC(NULL, screen);
    if (!g_logoLight || !g_logoDark) return;
    if (g_logoLight->GetLastStatus() != Gdiplus::Ok || g_logoDark->GetLastStatus() != Gdiplus::Ok) {
        delete g_logoLight; g_logoLight = NULL;
        delete g_logoDark; g_logoDark = NULL;
        return;
    }

    // The icon is a black outline; on a dark page the same shape has to be
    // white. Premultiplied black is (0,0,0,a), so white premultiplied is (a,a,a).
    Gdiplus::BitmapData bd;
    Gdiplus::Rect rc(0, 0, size, size);
    if (g_logoDark->LockBits(&rc, Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeWrite,
            PixelFormat32bppPARGB, &bd) == Gdiplus::Ok) {
        BYTE* base = (BYTE*)bd.Scan0;
        for (int y = 0; y < size; y++) {
            BYTE* row = base + y * bd.Stride;
            for (int x = 0; x < size; x++) {
                BYTE* p = row + x * 4;
                if (p[3] == 0) continue;
                p[0] = p[3];
                p[1] = p[3];
                p[2] = p[3];
            }
        }
        g_logoDark->UnlockBits(&bd);
    }
}

static void OpenSettings(void)
{
    LoadLogoBitmaps();

    if (g_setHwnd) {
        ShowWindow(g_setHwnd, SW_SHOW);
        SetForegroundWindow(g_setHwnd);
        SettingsInvalidate();
        return;
    }

    POINT origin0 = { 0, 0 };
    HMONITOR mon = MonitorFromPoint(origin0, MONITOR_DEFAULTTOPRIMARY);
    SettingsLayoutFor(mon, 0);
    double s = SettingsScale();

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = SettingsProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = kSettingsClass;
    RegisterClassExW(&wc);

    POINT origin = { 0, 0 };
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), &mi);
    int x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - g_setW) / 2;
    int y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - g_setH) / 2;

    // Borderless but frame-aware: WS_THICKFRAME (invisible) + DWM corner
    // preference gives the native Win11 rounded corners, drop shadow and
    // show/hide animation.  Older systems just get square corners - we do
    // not draw our own rounding.  The window is a normal window (not
    // WS_EX_LAYERED) so the DWM animations stay alive.
    g_setHwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kSettingsClass,
        L"轻谱 · 设置", WS_POPUP | WS_THICKFRAME, x, y, g_setW, g_setH,
        NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!g_setHwnd) return;

    {
        typedef HRESULT (WINAPI *PFN_DwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
        HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
        if (dwm) {
            PFN_DwmSetWindowAttribute fn = (PFN_DwmSetWindowAttribute)
                GetProcAddress(dwm, "DwmSetWindowAttribute");
            if (fn) {
                const DWORD kRound = 2;   // DWMWCP_ROUND
                fn(g_setHwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */,
                    &kRound, sizeof(kRound));
            }
        }
    }

    ShowWindow(g_setHwnd, SW_SHOW);
    SetForegroundWindow(g_setHwnd);
    SettingsInvalidate();
}

static void OpenSettingsPage(int page)
{
    g_setMode = 1;                       // 二级：设置
    g_setPage = ClampInt(page, 0, 2);    // 0 外观 / 1 显示内容 / 2 关于
    OpenSettings();
}


static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (g_taskbarCreatedMsg != 0 && msg == g_taskbarCreatedMsg) {
        g_trayAdded = false;
        AddTrayIcon(hwnd, (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
        return 0;
    }

    switch (msg) {
    case WM_CREATE:
        SetTimer(hwnd, 1, 60000, NULL);
        SetTimer(hwnd, 2, 800, NULL);
        SetTimer(hwnd, 4, 16, NULL);   // hover fade: cursor poll + animation
        return 0;

    case WM_TIMER:
        if (wParam == 1) {
            BuildModel();
            ComputeLayout();
            UpdateTheme();
            PositionWindow();
            Render();
            SendToDesktopLayer();
        } else if (wParam == 2) {
            ULONGLONG stamp = WallpaperStamp();
            if (stamp != g_wallpaperStamp) {
                g_wallpaperStamp = stamp;
                UpdateTheme();
                Render();
            }
            // "Show desktop" hides ordinary top-level windows and raises the
            // desktop WorkerW stack; a desktop widget should stay on the
            // wallpaper, so restore it and keep it anchored above that stack.
            if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) {
                ShowWindow(hwnd, SW_SHOWNOACTIVATE);
                PositionWindow();
                Render();
            }
            SendToDesktopLayer();
        } else if (wParam == 3) {
            KillTimer(hwnd, 3);
            BuildModel();
            ComputeLayout();
            UpdateTheme();
            PositionWindow();
            Render();
            SendToDesktopLayer();
        } else if (wParam == 4) {
            // ClassIsland-style hover: the panel is click-through, so poll the
            // cursor.  Over the panel -> fade out; off it -> fade back in.
            if (g_hwnd) {
                POINT pt;
                GetCursorPos(&pt);
                RECT rc;
                GetWindowRect(g_hwnd, &rc);
                bool inside = PtInRect(&rc, pt) != FALSE;
                int want = inside ? 0 : 255;
                if (want != g_hoverTarget) {
                    g_hoverTarget = want;
                    g_fadeStartTick = GetTickCount64();
                    g_fadeStartAlpha = g_hoverAlpha;
                }
                if (g_hoverAlpha != g_hoverTarget) {
                    ULONGLONG el = GetTickCount64() - g_fadeStartTick;
                    if (el >= kFadeMs) {
                        g_hoverAlpha = g_hoverTarget;
                    } else {
                        double t = (double)el / (double)kFadeMs;
                        g_hoverAlpha = (int)(g_fadeStartAlpha
                            + (g_hoverTarget - g_fadeStartAlpha) * t + 0.5);
                    }
                    PushHoverAlpha();
                }
            }
        }
        return 0;

    case WM_APP + 2:
        if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONDBLCLK)
            ShowTrayMenu(hwnd);
        else if (lParam == WM_LBUTTONUP) {
            // 左键单击托盘：打开主界面（硬件显示）
            g_setMode = 0;
            g_mainTab = 0;
            OpenSettings();
        }
        return 0;

    case WM_DEVICECHANGE:
        if (wParam == DBT_DEVNODES_CHANGED ||
            wParam == DBT_DEVICEARRIVAL ||
            wParam == DBT_DEVICEREMOVECOMPLETE) {
            SetTimer(hwnd, 3, 700, NULL);
        }
        return TRUE;

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    // Desktop widgets must survive "Show desktop" (Win+D), which minimizes
    // every top-level window.
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_MINIMIZE) return 0;
        break;

    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) {
            PostMessageW(hwnd, WM_APP + 1, 0, 0);
            return 0;
        }
        break;

    case WM_APP + 1:
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        PositionWindow();
        Render();
        SendToDesktopLayer();
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SETTINGCHANGE:
        // Work area may have changed (taskbar moved/resized) - refit.
        ComputeLayout();
        PositionWindow();
        g_wallpaperStamp = WallpaperStamp();
        UpdateTheme();
        Render();
        return 0;

    case WM_DISPLAYCHANGE:
        g_dpi = GetDpiSafe(hwnd);
        ComputeLayout();
        PositionWindow();
        Render();
        return 0;

    case WM_DPICHANGED: {
        UINT ndpi = (UINT)HIWORD(wParam);
        if (ndpi == 0) ndpi = (UINT)LOWORD(wParam);
        g_dpi = ndpi ? ndpi : GetDpiSafe(hwnd);
        ComputeLayout();
        PositionWindow();
        Render();
        return 0;
    }

    case WM_DPICHANGED_AFTERPARENT:
        g_dpi = GetDpiSafe(hwnd);
        ComputeLayout();
        PositionWindow();
        Render();
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        KillTimer(hwnd, 2);
        KillTimer(hwnd, 3);
        KillTimer(hwnd, 4);
        RemoveTrayIcon();
        g_hwnd = NULL;
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ===========================================================================
// Entry point
// ===========================================================================

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int)
{
    // Headless --dump / --dump-settings runs are inspection renders: they
    // must not trip the single-instance lock (an already-running HSpec on
    // the desktop would silently swallow them and no dump gets written).
    bool headlessDump = false;
    {
        int pargc = 0;
        LPWSTR* pargv = CommandLineToArgvW(GetCommandLineW(), &pargc);
        if (pargv) {
            for (int i = 1; i < pargc; i++) {
                if (wcscmp(pargv[i], L"--dump") == 0 ||
                    wcscmp(pargv[i], L"--dump-settings") == 0) {
                    headlessDump = true;
                    break;
                }
            }
            LocalFree(pargv);
        }
    }

    HANDLE mutex = NULL;
    if (!headlessDump) {
        mutex = CreateMutexW(NULL, FALSE, L"HSpec_Singleton");
        if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
            CloseHandle(mutex);
            return 0;
        }
    }

    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv) {
            for (int i = 1; i < argc; i++) {
                if (wcscmp(argv[i], L"--dump") == 0 && i + 1 < argc) {
                    wcsncpy_s(g_dumpPath, MAX_PATH, argv[++i], _TRUNCATE);
                } else if (wcscmp(argv[i], L"--dump-settings") == 0 && i + 1 < argc) {
                    wcsncpy_s(g_settingsDumpPath, MAX_PATH, argv[++i], _TRUNCATE);
                } else if (wcscmp(argv[i], L"--page") == 0 && i + 1 < argc) {
                    g_settingsDumpPage = ClampInt(_wtoi(argv[++i]), 0, 3);
                } else if (wcscmp(argv[i], L"--theme") == 0 && i + 1 < argc) {
                    // applied after LoadConfig(), which would overwrite it
                    g_themeOverride = ClampInt(_wtoi(argv[++i]), 0, THEME_COUNT - 1);
                } else if (wcscmp(argv[i], L"--main") == 0 && i + 1 < argc) {
                    g_mainTab = ClampInt(_wtoi(argv[++i]), 0, 1);
                } else if (wcscmp(argv[i], L"--tab") == 0 && i + 1 < argc) {
                    g_dumpSetMode = 1;
                    g_settingsDumpPage = ClampInt(_wtoi(argv[++i]), 0, 2);
                } else if (wcscmp(argv[i], L"--dump-info") == 0 && i + 1 < argc) {
                    wcsncpy_s(g_infoDumpPath, MAX_PATH, argv[++i], _TRUNCATE);
                } else if (wcscmp(argv[i], L"--zoom") == 0 && i + 1 < argc) {
                    double z = _wtof(argv[++i]);
                    g_dumpZoom = (z >= 1.0 && z <= 4.0) ? z : 1.0;
                } else if (wcscmp(argv[i], L"--settings") == 0) {
                    g_openSettingsAtStart = true;
                }
            }
            LocalFree(argv);
        }
    }

    Gdiplus::GdiplusStartupInput gdiIn;
    if (Gdiplus::GdiplusStartup(&g_gdiToken, &gdiIn, NULL) != Gdiplus::Ok)
        return 1;

    LoadEmbeddedFont(hInstance);
    LoadIdTables(hInstance);
    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");
    LoadConfig();
    if (g_themeOverride >= 0) g_cfg.theme = g_themeOverride;
    BuildModel();

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"HSpecWindow";
    if (!RegisterClassExW(&wc)) {
        Gdiplus::GdiplusShutdown(g_gdiToken);
        return 1;
    }

    g_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        L"HSpecWindow", L"HSpec 轻谱", WS_POPUP,
        0, 0, 100, 100, NULL, NULL, hInstance, NULL);
    if (!g_hwnd) {
        Gdiplus::GdiplusShutdown(g_gdiToken);
        return 1;
    }

    g_dpi = GetDpiSafe(g_hwnd);
    ComputeLayout();
    UpdateTheme();
    g_wallpaperStamp = WallpaperStamp();
    PositionWindow();
    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    Render();
    SendToDesktopLayer();
    AddTrayIcon(g_hwnd, hInstance);

    if (g_openSettingsAtStart) OpenSettings();

    if (g_infoDumpPath[0]) {
        TargetInfo ti;
        CollectTargetInfo(&ti);
        bool ok = SaveTargetInfo(g_infoDumpPath, &ti);
        return ok ? 0 : 1;
    }

    if (g_settingsDumpPath[0] && !g_dumpPath[0]) {
        POINT origin0 = { 0, 0 };
        SettingsLayoutFor(
            MonitorFromPoint(origin0, MONITOR_DEFAULTTOPRIMARY), 0);
        g_setMode = g_dumpSetMode;
        g_setPage = g_settingsDumpPage;
        LoadLogoBitmaps();
        SettingsSavePng(g_settingsDumpPath);
    }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (mutex) CloseHandle(mutex);
    if (g_fontReg) {
        RemoveFontMemResourceEx(g_fontReg);
        g_fontReg = NULL;
    }
    if (g_gdipFonts) {
        delete g_gdipFonts;
        g_gdipFonts = NULL;
    }
    Gdiplus::GdiplusShutdown(g_gdiToken);
    return 0;
}
