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

static int GpuRank(const wchar_t* name)
{
    static const wchar_t* discrete[] = {
        L"GeForce", L"Radeon RX", L"Radeon Pro", L"Quadro", L"RTX", L"GTX", L"Arc"
    };
    for (int i = 0; i < 7; i++)
        if (FindNoCase((wchar_t*)name, discrete[i])) return 0;
    return 1;
}

struct GpuInfo { wchar_t name[192]; wchar_t key[9]; int rank; };

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

enum { THEME_PLAIN = 0, THEME_CARD = 1, THEME_ETHEREAL = 2 };

struct Config
{
    int theme;
    int hueFollow;
    int hue;
    int opacity;
    int radius;
    int showCpu, showBoard, showMem, showDisk, showGpu, showNet;
};

static Config g_cfg = { THEME_PLAIN, 1, 250, 88, 16, 1, 1, 1, 1, 1, 1 };
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
    g_cfg.theme = ClampInt((int)GetPrivateProfileIntW(L"panel", L"theme", g_cfg.theme, g_iniPath), 0, 2);
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
    g_skipSection = !g_cfg.showBoard;
    wchar_t boardTitle[256] = L"主板";
    const wchar_t* boardProduct = g_smbios.boardProduct[0] ? g_smbios.boardProduct
        : (g_smbios.sysProduct[0] ? g_smbios.sysProduct : L"");
    if (boardProduct[0])
        WPrintf(boardTitle, 256, L"主板: %s", boardProduct);
    r = ModelAddRow(ICON_BOARD, boardTitle);

    if (g_smbios.boardManufacturer[0]) {
        wchar_t mfr[128];
        wcsncpy_s(mfr, 128, g_smbios.boardManufacturer, _TRUNCATE);
        TranslateVendorText(mfr, 128);
        wchar_t d[256];
        WPrintf(d, 256, L"厂商: %s", mfr);
        ModelAddDetail(r, d);
    }
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
        wchar_t d[192];
        WPrintf(d, 192, L"启动: %s", boot);
        ModelAddDetail(r, d);
    }

    if (fw == 2 && sb == 1) {
        wchar_t cert[64] = { 0 };
        if (CollectBootCert(cert, 64)) {
            wchar_t d[128];
            WPrintf(d, 128, L"证书: %s", cert);
            ModelAddDetail(r, d);
        }
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

    // ---- GPU ----
    g_skipSection = !g_cfg.showGpu;
    GpuInfo gpus[4];
    int gc = EnumGpus(gpus, 4);
    for (int i = 0; i < gc; i++) {
        wchar_t title[256];
        WPrintf(title, 256, L"显卡: %s", gpus[i].name);
        ModelAddRow(ICON_GPU, title);
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
static int g_winW = 420;
static int g_winH = 320;
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
static int PanelMarginX(void) { return (int)(18.0 * (double)g_dpi / 96.0 + 0.5); }
static int PanelMarginY(void) { return (int)(22.0 * (double)g_dpi / 96.0 + 0.5); }

static void RefreshWallpaperCache(void);

// Pick dark or light text from the wallpaper brightness behind the panel.
static void UpdateTheme(void)
{
    wchar_t path[MAX_PATH] = { 0 };
    bool haveImage = false;

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
static bool CardBlur(void) { return g_cfg.theme == THEME_ETHEREAL; }

static void ComputeLayout(void)
{
    double s = (double)g_dpi / 96.0;
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
    // A card spends padding on both sides, so the window widens just enough to
    // keep the text column identical to the plain theme (no new truncation).
    g_winW = (int)((CardMode() ? 526.0 : 480.0) * s + 0.5);

    int h = CardMode() ? (g_cardMargin * 2 + g_padY * 2) : g_padTop;
    for (int i = 0; i < g_rowCount; i++)
        h += g_titleH + g_rows[i].detailCount * g_detailH + g_rowGap;
    if (!CardMode()) h += (int)(4.0 * s + 0.5);
    g_winH = h;
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
// Card layer (THEME_CARD / THEME_ETHEREAL)
// ---------------------------------------------------------------------------
//
// GDI+ has no backdrop blur. The panel sits on the desktop layer, so its
// backdrop *is* the wallpaper: keep one downscaled copy of it around and blur
// the card area with a down/up scale, only when the wallpaper changes.

static Gdiplus::Bitmap* g_wallCache = NULL;
static int g_wallCacheW = 0, g_wallCacheH = 0;

static void RefreshWallpaperCache(void)
{
    if (g_wallCache) {
        delete g_wallCache;
        g_wallCache = NULL;
    }
    g_wallCacheW = g_wallCacheH = 0;
    if (!CardBlur()) return;  // a flat card never reads the wallpaper

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
    if (dpiScale <= 0.0) dpiScale = (double)g_dpi / 96.0;

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
    double s = (double)g_dpi / 96.0;

    Gdiplus::Bitmap bmp(g_winW, g_winH, PixelFormat32bppPARGB);
    if (bmp.GetLastStatus() != Gdiplus::Ok) return false;
    Gdiplus::Graphics g(&bmp);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    g.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
    g.Clear(Gdiplus::Color(0, 0, 0, 0));

    const bool card = CardMode();
    const int cardAlpha = ClampInt(g_cfg.opacity, 40, 100) * 255 / 100;
    Palette pal = MakePalette(g_lightText, cardAlpha);

    const Gdiplus::Color titleColor = card ? pal.title : g_titleColor;
    const Gdiplus::Color detailColor = card ? pal.detail : g_detailColor;
    const Gdiplus::Color iconColor = card ? pal.chipFg : g_iconColor;
    const bool lightText = card ? pal.darkCard : g_lightText;

    Gdiplus::RectF cardRect((float)g_cardMargin, (float)g_cardMargin,
        (float)(g_winW - g_cardMargin * 2), (float)(g_winH - g_cardMargin * 2));
    float radius = (float)g_cfg.radius * (float)s;
    if (radius > cardRect.Height * 0.5f) radius = cardRect.Height * 0.5f;

    if (card) {
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
    float textX = card
        ? (float)(g_cardMargin + g_padX + g_chipSize + g_gapX)
        : (float)(g_iconSize + g_gapX);
    float textW = (float)g_winW - textX - (float)(card ? g_cardMargin + g_padX : 4.0 * s);

    for (int i = 0; i < g_rowCount; i++) {
        UiRow& row = g_rows[i];
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
        y += (float)g_rowGap;
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
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat = AC_SRC_ALPHA;

    SIZE size;
    size.cx = g_winW;
    size.cy = g_winH;
    POINT src = { 0, 0 };
    BOOL ok = UpdateLayeredWindow(g_hwnd, screen, NULL, &size, mem, &src, 0, &bf, ULW_ALPHA);

    SelectObject(mem, old);
    DeleteDC(mem);
    DeleteObject(hbm);
    ReleaseDC(NULL, screen);
    return ok != FALSE;
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
    int x = mi.rcMonitor.right - g_winW - marginX;
    int y = mi.rcMonitor.top + marginY;
    if (x < mi.rcMonitor.left) x = mi.rcMonitor.left;

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
        OpenSettings();
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

static double SettingsScale(void) { return (double)g_setDpi / 96.0; }

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
static int g_setPage = 0;       // 0 look, 1 content, 2 about
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
    ACT_TOGGLE = 40,     // + toggle index
    ACT_GITHUB = 50,
    ACT_COPYVER = 51,
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

static void DrawNavIcon(Gdiplus::Graphics& g, int kind, float x, float y, float sz,
    const Gdiplus::Color& c)
{
    Gdiplus::Pen pen(c, 1.4f);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    float u = sz / 24.0f;
    if (kind == 0) {  // look: circle with four dots
        g.DrawEllipse(&pen, x + 3 * u, y + 3 * u, 18 * u, 18 * u);
        Gdiplus::SolidBrush dot(c);
        float pts[4][2] = { { 8.5f, 9 }, { 15.5f, 8 }, { 9.5f, 15.5f }, { 16, 14 } };
        for (int i = 0; i < 4; i++)
            g.FillEllipse(&dot, x + (pts[i][0] - 1.1f) * u, y + (pts[i][1] - 1.1f) * u,
                2.2f * u, 2.2f * u);
    } else if (kind == 1) {  // list
        g.DrawLine(&pen, x + 9 * u, y + 6 * u, x + 20 * u, y + 6 * u);
        g.DrawLine(&pen, x + 9 * u, y + 12 * u, x + 20 * u, y + 12 * u);
        g.DrawLine(&pen, x + 9 * u, y + 18 * u, x + 20 * u, y + 18 * u);
        Gdiplus::SolidBrush dot(c);
        for (int i = 0; i < 3; i++)
            g.FillEllipse(&dot, x + 3.4f * u, y + (6 + i * 6 - 1.2f) * u, 2.4f * u, 2.4f * u);
    } else {  // info
        g.DrawEllipse(&pen, x + 3 * u, y + 3 * u, 18 * u, 18 * u);
        g.DrawLine(&pen, x + 12 * u, y + 10.5f * u, x + 12 * u, y + 16.5f * u);
        Gdiplus::SolidBrush dot(c);
        g.FillEllipse(&dot, x + 10.8f * u, y + 6.6f * u, 2.4f * u, 2.4f * u);
    }
}

// ---------------------------------------------------------------------------
// Pages
// ---------------------------------------------------------------------------

static void PageLook(Gdiplus::Graphics& g, const Palette& pal, bool light,
    float x, float y, float w, float s)
{
    const float pad = 20 * s;
    Gdiplus::Color sub = pal.detail;

    // --- 主题 ---
    const wchar_t* names[3] = { L"纯文字", L"轻卡片", L"Ethereal" };
    float gap = 16 * s;
    float tileW = (w - 40 * s - 2 * gap) / 3.0f;
    float thumbH = tileW * 84.0f / 161.0f;
    float cardH1 = pad + 26 * s + 30 * s + thumbH + 8 * s + 24 * s + pad;
    Gdiplus::RectF cardC1(x, y, w, cardH1);
    FillRound(g, cardC1, 14 * s, pal.card);
    float cy = y + pad;
    SText(g, L"主题", 18, FW_NORMAL, pal.title, light, x + 20 * s, cy, w - 40 * s, 24 * s);
    cy += 26 * s;
    SText(g, L"同一个信息模型，三种渲染层", 14, FW_NORMAL, sub, light,
        x + 20 * s, cy, w - 40 * s, 20 * s);
    cy += 30 * s;
    for (int i = 0; i < 3; i++) {
        float tx = x + 20 * s + i * (tileW + gap);
        Gdiplus::RectF thumb(tx, cy, tileW, thumbH);
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
            tx, cy + thumbH + 8 * s, tileW - 22 * s, 22 * s);
        // radio dot
        float dotR = 6.5f * s;
        float dx = tx + tileW - dotR * 2, dy = cy + thumbH + 8 * s + (22 * s - dotR * 2) * 0.5f;
        Gdiplus::Pen dotPen((g_cfg.theme == i) ? pal.primary : pal.chipBg, 1.8f * s);
        g.DrawEllipse(&dotPen, dx, dy, dotR * 2, dotR * 2);
        if (g_cfg.theme == i) {
            Gdiplus::SolidBrush dotB(pal.primary);
            g.FillEllipse(&dotB, dx + 3.5f * s, dy + 3.5f * s, dotR * 2 - 7 * s, dotR * 2 - 7 * s);
        }
        HitAdd((int)tx, (int)thumb.Y, (int)tileW, (int)(thumb.Height + 32 * s), ACT_THEME + i);
    }

    // --- 主色 ---
    float y2 = cardC1.GetBottom() + 14 * s;
    Gdiplus::RectF cardC2(x, y2, w, 172 * s);
    FillRound(g, cardC2, 14 * s, pal.card);
    float cy2 = y2 + pad;
    SText(g, L"主色", 18, FW_NORMAL, pal.title, light, x + 20 * s, cy2, w - 40 * s, 24 * s);
    cy2 += 28 * s;

    SText(g, L"跟随壁纸取色", 15, FW_NORMAL, pal.title, light, x + 20 * s, cy2,
        w - 140 * s, 22 * s);
    SText(g, L"从壁纸采样主色，换算成 oklch 的 hue", 13.5f, FW_NORMAL, sub, light,
        x + 20 * s, cy2 + 22 * s, w - 140 * s, 18 * s);
    Gdiplus::RectF sw(x + w - 20 * s - 44 * s, cy2 + 2 * s, 44 * s, 24 * s);
    DrawSwitch(g, sw, g_cfg.hueFollow != 0, pal);
    HitAdd((int)sw.X, (int)sw.Y, (int)sw.Width, (int)sw.Height, ACT_HUE_FOLLOW);

    float hueY = cy2 + 52 * s;
    Gdiplus::RectF hue(x + 20 * s, hueY, w - 40 * s, 22 * s);
    DrawHueStrip(g, hue, (float)HueDeg(), pal);
    g_hueTrack = RECT{ (LONG)hue.X, (LONG)hue.Y, (LONG)hue.GetRight(), (LONG)hue.GetBottom() };
    HitAdd((int)hue.X - 4, (int)hue.Y - 6, (int)hue.Width + 8, (int)hue.Height + 12, ACT_HUE_STRIP);

    wchar_t hueText[64];
    WPrintf(hueText, 64, L"Hue %d · %s", HueDeg(),
        g_cfg.hueFollow ? L"取自壁纸" : L"手动");
    SText(g, hueText, 13.5f, FW_NORMAL, sub, light, x + 20 * s, hueY + 30 * s,
        w - 120 * s, 18 * s);

    // --- 面板 ---
    float y3 = cardC2.GetBottom() + 14 * s;
    Gdiplus::RectF cardC3(x, y3, w, 150 * s);
    FillRound(g, cardC3, 14 * s, pal.card);
    float cy3 = y3 + pad;
    SText(g, L"面板", 18, FW_NORMAL, pal.title, light, x + 20 * s, cy3, w - 40 * s, 24 * s);
    cy3 += 32 * s;

    struct { const wchar_t* name; int value; int act; int minV; int maxV; const wchar_t* unit; } rows[2] = {
        { L"卡片不透明度", g_cfg.opacity, ACT_OPACITY, 40, 100, L"%" },
        { L"圆角", g_cfg.radius, ACT_RADIUS, 0, 24, L"px" },
    };
    for (int i = 0; i < 2; i++) {
        float ry = cy3 + i * 38 * s;
        SText(g, rows[i].name, 15, FW_NORMAL, pal.title, light, x + 20 * s, ry,
            150 * s, 24 * s);
        Gdiplus::RectF track(x + 20 * s + 150 * s, ry + 8 * s,
            w - 40 * s - 150 * s - 64 * s, 9 * s);
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
}

static void PageContent(Gdiplus::Graphics& g, const Palette& pal, bool light,
    float x, float y, float w, float s)
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

    const float rowH = 84 * s;
    Gdiplus::RectF card(x, y, w, TOGGLE_COUNT * rowH + 24 * s);
    FillRound(g, card, 14 * s, pal.card);

    for (int i = 0; i < TOGGLE_COUNT; i++) {
        float ry = y + 12 * s + i * rowH;
        if (i > 0) {
            Gdiplus::Pen line(pal.divider, 1.0f);
            g.DrawLine(&line, x + 20 * s, ry - 8 * s, card.GetRight() - 20 * s, ry - 8 * s);
        }

        // 84x48 preview of what the panel row will look like.
        Gdiplus::RectF prev(x + 20 * s, ry + 18 * s, 84 * s, 48 * s);
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
            x + 122 * s, ry + 18 * s, w - 122 * s - 84 * s, 24 * s);
        SText(g, metas[i], 13.5f, FW_NORMAL, pal.detail, light,
            x + 122 * s, ry + 46 * s, w - 122 * s - 84 * s, 20 * s);

        Gdiplus::RectF sw(card.GetRight() - 20 * s - 44 * s, ry + 30 * s, 44 * s, 24 * s);
        DrawSwitch(g, sw, ToggleValue(i) != 0, pal);
        HitAdd((int)(x + 10 * s), (int)(ry + 4 * s), (int)(w - 20 * s), (int)(rowH - 8 * s),
            ACT_TOGGLE + i);
    }
}

static void PageAbout(Gdiplus::Graphics& g, const Palette& pal, bool light,
    float x, float y, float w, float s)
{
    float cx = x + w * 0.5f;

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

static void SettingsDraw(Gdiplus::Graphics& g)
{
    double s = SettingsScale();
    Palette pal = MakePalette(g_lightText, 255);
    const bool light = pal.darkCard;   // light text == dark page

    // Full-bleed square background: the window corners are DWM's (see
    // OpenSettings - DWMWCP_ROUND on Win11, plain square on older systems),
    // and the drop shadow / show animation are the system's, not ours.
    Gdiplus::RectF page(0, 0, (float)g_setW, (float)g_setH);
    g.Clear(pal.page);

    float px = 24 * (float)s;
    float py = 14 * (float)s;

    // ---- title bar (drag region; only the close button is interactive) ----
    {
        Gdiplus::Bitmap* logo = light ? g_logoDark : g_logoLight;
        if (logo && logo->GetLastStatus() == Gdiplus::Ok)
            g.DrawImage(logo, Gdiplus::RectF(px, py + 4 * s, 22 * s, 22 * s));
        SText(g, L"轻谱 · 设置", 14, FW_NORMAL, pal.detail, light,
            px + 30 * s, py, 240 * s, 30 * s);

        Gdiplus::RectF close(page.GetRight() - 64 * s, py + 1 * s, 40 * s, 30 * s);
        if (g_setHot == ACT_CLOSE) FillRound(g, close, 8 * s, pal.chipBg);
        Gdiplus::Pen cp(pal.detail, 1.6f);
        cp.SetStartCap(Gdiplus::LineCapRound);
        cp.SetEndCap(Gdiplus::LineCapRound);
        float cxm = close.X + close.Width * 0.5f, cym = close.Y + close.Height * 0.5f;
        float d5 = 6.0f * (float)s;
        g.DrawLine(&cp, cxm - d5, cym - d5, cxm + d5, cym + d5);
        g.DrawLine(&cp, cxm + d5, cym - d5, cxm - d5, cym + d5);
        HitAdd((int)close.X, (int)close.Y, (int)close.Width, (int)close.Height, ACT_CLOSE);
    }

    // ---- sidebar ----
    float navX = 20 * (float)s;
    float navY = 76 * (float)s;
    const wchar_t* navNames[3] = { L"外观", L"显示内容", L"关于" };
    for (int i = 0; i < 3; i++) {
        Gdiplus::RectF item(navX, navY + i * 54 * s, 200 * s, 46 * s);
        bool on = (g_setPage == i);
        if (on) FillRound(g, item, 12 * s, pal.itemBg);
        Gdiplus::Color fg = on ? pal.itemFg : pal.detail;
        DrawNavIcon(g, i == 2 ? 2 : i, item.X + 14 * s, item.Y + 13 * s, 18 * s, fg);
        SText(g, navNames[i], 15, FW_NORMAL, fg, light,
            item.X + 46 * s, item.Y, item.Width - 56 * s, item.Height);
        HitAdd((int)item.X, (int)item.Y, (int)item.Width, (int)item.Height, ACT_NAV + i);
    }

    wchar_t foot[64];
    WPrintf(foot, 64, L"HSpec %s", kAppVersion);
    SText(g, foot, 12, FW_NORMAL, pal.detail, light, navX + 6 * s,
        page.GetBottom() - 44 * s, 160 * s, 18 * s);

    // ---- content ----
    float cxp = 240 * (float)s;
    float cyp = 28 * (float)s;
    float cwp = page.Width - 240 * s - 32 * s;

    // page heading with the theme's accent bar
    const wchar_t* titles[3] = { L"外观", L"显示内容", L"关于" };
    if (g_setPage != 2) {
        FillRound(g, MRect(cxp, cyp + 6 * s, 4 * s, 24 * s), 2 * s, pal.primary);
        SText(g, titles[g_setPage], 24, FW_NORMAL, pal.title, light, cxp + 16 * s, cyp,
            cwp - 16 * s, 32 * s);
        SText(g, g_setPage == 0 ? L"面板画在桌面层上，这里决定它离壁纸有多近"
                                : L"关掉的项目不再出现在面板上",
            14, FW_NORMAL, pal.detail, light, cxp + 16 * s, cyp + 34 * s,
            cwp - 16 * s, 20 * s);
        cyp += 68 * s;
    }

    if (g_setPage == 0) PageLook(g, pal, light, cxp, cyp, cwp, (float)s);
    else if (g_setPage == 1) PageContent(g, pal, light, cxp, cyp, cwp, (float)s);
    else PageAbout(g, pal, light, cxp, cyp + 30 * s, cwp, (float)s);
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
        if (v != g_cfg.opacity) { g_cfg.opacity = v; PanelReload(false); }
    } else if (act == ACT_RADIUS) {
        int v = (int)(t * 24.0f + 0.5f);
        if (v != g_cfg.radius) { g_cfg.radius = v; PanelReload(false); }
    } else if (act == ACT_HUE_STRIP) {
        int v = (int)(t * 360.0f + 0.5f);
        g_cfg.hueFollow = 0;
        if (v != g_cfg.hue) { g_cfg.hue = v; PanelReload(false); }
    }
}

static void PerformAction(int act)
{
    if (act >= ACT_NAV && act < ACT_NAV + 3) {
        g_setPage = act - ACT_NAV;
    } else if (act >= ACT_THEME && act < ACT_THEME + 3) {
        int t = act - ACT_THEME;
        if (t != g_cfg.theme) { g_cfg.theme = t; PanelReload(false); }
    } else if (act >= ACT_TOGGLE && act < ACT_TOGGLE + TOGGLE_COUNT) {
        ToggleFlip(act - ACT_TOGGLE);
        PanelReload(true);
    } else if (act == ACT_HUE_FOLLOW) {
        g_cfg.hueFollow = !g_cfg.hueFollow;
        PanelReload(false);
    } else if (act == ACT_CLOSE) {
        PostMessageW(g_setHwnd, WM_CLOSE, 0, 0);
        return;
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
            PanelReload(false);
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
        if (wParam == TRUE) return 0;
        break;

    case WM_NCHITTEST: {
        // Interactive controls get the client; everything else is the drag
        // region - the window is borderless, so this is how it moves.
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &pt);
        if (HitTest(pt.x, pt.y) != ACT_NONE) return HTCLIENT;
        return HTCAPTION;
    }

    case WM_DPICHANGED: {
        g_setDpi = (UINT)HIWORD(wParam);
        if (g_setDpi == 0) g_setDpi = (UINT)LOWORD(wParam);
        double ds = SettingsScale();
        g_setW = (int)(1024.0 * ds + 0.5);
        g_setH = (int)(720.0 * ds + 0.5);
        RECT* target = (RECT*)lParam;
        SetWindowPos(hwnd, NULL, target->left, target->top, g_setW, g_setH,
            SWP_NOZORDER | SWP_NOACTIVATE);
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
    UINT mdpi = MonitorDpi(mon);
    g_setDpi = mdpi ? mdpi : GetDpiSafe(g_hwnd);
    double s = SettingsScale();
    g_setW = (int)(1024.0 * s + 0.5);
    g_setH = (int)(720.0 * s + 0.5);

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
        }
        return 0;

    case WM_APP + 2:
        if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONDBLCLK)
            ShowTrayMenu(hwnd);
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

    case WM_DPICHANGED:
        g_dpi = GetDpiSafe(hwnd);
        ComputeLayout();
        PositionWindow();
        Render();
        return 0;

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
                    g_settingsDumpPage = ClampInt(_wtoi(argv[++i]), 0, 2);
                } else if (wcscmp(argv[i], L"--theme") == 0 && i + 1 < argc) {
                    // applied after LoadConfig(), which would overwrite it
                    g_themeOverride = ClampInt(_wtoi(argv[++i]), 0, 2);
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

    if (g_settingsDumpPath[0] && !g_dumpPath[0]) {
        POINT origin0 = { 0, 0 };
        UINT mdpi = MonitorDpi(MonitorFromPoint(origin0, MONITOR_DEFAULTTOPRIMARY));
        g_setDpi = mdpi ? mdpi : GetDpiSafe(g_hwnd);
        double ds = SettingsScale();
        g_setW = (int)(1024.0 * ds + 0.5);
        g_setH = (int)(720.0 * ds + 0.5);
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
