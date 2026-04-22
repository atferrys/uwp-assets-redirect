// ==WindhawkMod==
// @id              uwp-assets-redirect
// @name            UWP Assets Redirect
// @description     Replace UWP apps assets (like app icons) without having to worry about updates or changing system files permissions.
// @version         0.1
// @author          ferrys
// @github          https://github.com/atferrys
// @license         GPL-3.0
// @include         explorer.exe
// @include         StartMenuExperienceHost.exe
// @include         SearchHost.exe
// @include         ShellExperienceHost.exe
// @include         ShellHost.exe
// @include         RuntimeBroker.exe
// @include         Taskmgr.exe
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# UWP Assets Redirect
Replace UWP apps assets (like app icons) without having to worry about updates
or changing system files permissions.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- windows-apps: 
  - - bundle: "Microsoft.WindowsCalculator"
      $name: Bundle name
      $description: >-
        The application bundle without any version appended.

        You can get easily get this via Task Manager, follow the guide in
        the details tab for more information.
    - assets-folder: "Assets"
      $name: Assets folder
    - redirect: "C:\\Custom Icons\\Calculator"
      $name: Redirection folder
      $description: The folder with the custom assets files.
  $name: WindowsApps Redirections
  $description: >-
    Redirections for the apps found in "C:\Program Files\WindowsApps".

    This is used for Microsoft Store apps and some system apps.

- system-apps: 
  - - bundle: "Microsoft.PPIProjection"
      $name: Bundle name
      $description: >-
        The application bundle without any version appended.

        You can get easily get this via Task Manager, follow the guide in
        the details tab for more information.
    - assets-folder: "Assets"
      $name: Assets folder
    - redirect: "C:\\Custom Icons\\Wireless Display"
      $name: Redirection folder
      $description: The folder with the custom assets files.
  $name: SystemApps Redirections
  $description: >-
    Redirections for the apps found in "C:\Windows\SystemApps".

    This is used for some system apps like "Wireless Display".
*/
// ==/WindhawkModSettings==

#include <windhawk_utils.h>
#include <windows.h>
#include <winternl.h>
#include <shlobj.h>
#include <format>
#include <string>
#include <unordered_map>

std::unordered_map<std::wstring, std::wstring> g_redirections;

template <typename T>
bool match(const T* pattern, size_t pattern_length, const T* str, size_t string_length, size_t& after_match_index) {
    size_t pattern_index = 0;
    size_t string_index = 0;

    // Flag used to only check first wildcard
    bool wildcard_matched = false;

    while (pattern_index < pattern_length && string_index < string_length) {
        if (!wildcard_matched && pattern[pattern_index] == '*') {
            ++pattern_index; // Skip the wildcard symbol
            wildcard_matched = true;

            // The rest of the pattern after the wildcard
            size_t rest_start = pattern_index;

            // Find the literal string after the wildcard (until the end)
            size_t literal_len = pattern_length - rest_start;

            // If the wildcard is the last thing present in the pattern
            if (literal_len == 0) {
                // Move forward until the first backslash or end of string
                while (string_index < string_length && str[string_index] != '\\') {
                    ++string_index;
                }

                after_match_index = string_index;
                return true;
            }

            // Try to match this literal in the string
            size_t match_pos = string_index;
            bool found = false;
            while (match_pos + literal_len <= string_length) {

                bool mismatch = false;
                for (size_t k = 0; k < literal_len; ++k) {
                    if (str[match_pos + k] != pattern[rest_start + k]) {
                        mismatch = true;
                        break;
                    }
                }
                
                if (!mismatch) {
                    found = true;
                    break;
                }

                // Stop wildcard matching at '\'
                if (str[match_pos] == '\\') break;

                ++match_pos;
            }

            if (!found) return false;

            // Move past the matched literal
            string_index = match_pos + literal_len;
            pattern_index = rest_start + literal_len;

            // Make after_match_index point to the end of the matched pattern
            after_match_index = string_index;
        } else {
            if (pattern[pattern_index] != str[string_index]) return false;
            ++pattern_index;
            ++string_index;
        }
    }

    // Ensure full pattern was matched
    return pattern_index == pattern_length;
}

using NtCreateFile_t = decltype(&NtCreateFile);
NtCreateFile_t NtCreateFile_Original;

NTSTATUS NTAPI NtCreateFile_Hook(
    PHANDLE FileHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes,
    PIO_STATUS_BLOCK IoStatusBlock,
    PLARGE_INTEGER AllocationSize,
    ULONG FileAttributes,
    ULONG ShareAccess,
    ULONG CreateDisposition,
    ULONG CreateOptions,
    PVOID EaBuffer,
    ULONG EaLength
) {

    UNICODE_STRING* ObjectName;
    std::wstring originalPath;

    if(ObjectAttributes && ObjectAttributes->ObjectName) {
        ObjectName = ObjectAttributes->ObjectName;
        originalPath.assign(ObjectName->Buffer, ObjectName->Length / sizeof(WCHAR));
    }

    // Check if we should redirect
    if(!originalPath.empty()) {

        std::wstring redirectPath;

        const wchar_t* path = originalPath.c_str();
        size_t pathLength = originalPath.length();

        for(const auto& pair : g_redirections) {
            
            const wchar_t* pattern = pair.first.c_str();
            size_t patternLength = pair.first.length();

            // If there are no wildcards in pattern, simply
            // use the end of the pattern as the match_end_index
            size_t match_end_index = patternLength;

            if(match(pattern, patternLength, path, pathLength, match_end_index)) {
                redirectPath = pair.second + originalPath.substr(match_end_index);
                break;
            }

        }

        if(!redirectPath.empty()) {

            Wh_Log(L"[Redirect Attempt] %s -> %s", originalPath.c_str(), redirectPath.c_str());

            ObjectName->Buffer = (PWSTR) redirectPath.c_str();
            ObjectName->Length = (USHORT) (redirectPath.length() * sizeof(WCHAR));
            ObjectName->MaximumLength = ObjectName->Length;

            NTSTATUS result = NtCreateFile_Original(
                FileHandle,
                DesiredAccess,
                ObjectAttributes,
                IoStatusBlock,
                AllocationSize,
                FileAttributes,
                ShareAccess,
                CreateDisposition,
                CreateOptions,
                EaBuffer,
                EaLength
            );

            if(NT_SUCCESS(result)) {
                Wh_Log(L"[Redirect Success] Redirected to: %s", redirectPath.c_str());
                return result;
            }

            Wh_Log(L"[Redirect Fail] Failed with code 0x%08X. Rolling back to original: %s", result, originalPath.c_str());

            ObjectName->Buffer = (PWSTR) originalPath.c_str();
            ObjectName->Length = (USHORT) (originalPath.length() * sizeof(WCHAR));
            ObjectName->MaximumLength = ObjectName->Length;
            
        }

    }

    return NtCreateFile_Original(
        FileHandle,
        DesiredAccess,
        ObjectAttributes,
        IoStatusBlock,
        AllocationSize,
        FileAttributes,
        ShareAccess,
        CreateDisposition,
        CreateOptions,
        EaBuffer,
        EaLength
    );
}

// Check if mod is hooked to the explorer process that owns the taskbar.
// Useful for running specific code once. Taken from:
// https://github.com/ramensoftware/windhawk-mods/blob/43fe260f5738d61ba09018c7de03863defbf40a0/mods/icon-resource-redirect.wh.cpp#L2120-L2122

bool IsExplorerProcess() {
    WCHAR path[MAX_PATH];
    if (!GetWindowsDirectory(path, ARRAYSIZE(path))) {
        Wh_Log(L"GetWindowsDirectory failed");
        return false;
    }

    wcscat_s(path, MAX_PATH, L"\\explorer.exe");

    return GetModuleHandle(path) == GetModuleHandle(nullptr);
}

HWND FindCurrentProcessTaskbarWnd() {
    HWND hTaskbarWnd = nullptr;

    EnumWindows(
        [](HWND hWnd, LPARAM lParam) WINAPI -> BOOL {
            DWORD dwProcessId;
            WCHAR className[32];
            if (GetWindowThreadProcessId(hWnd, &dwProcessId) &&
                dwProcessId == GetCurrentProcessId() &&
                GetClassName(hWnd, className, ARRAYSIZE(className)) &&
                _wcsicmp(className, L"Shell_TrayWnd") == 0) {
                *reinterpret_cast<HWND*>(lParam) = hWnd;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&hTaskbarWnd));

    return hTaskbarWnd;
}

bool DoesCurrentProcessOwnTaskbar() {
    return IsExplorerProcess() && FindCurrentProcessTaskbarWnd();
}

void RefreshIcons() {

    // Only run this once
    if (!DoesCurrentProcessOwnTaskbar()) {
        return;
    }

    // Let other processes some time to init/uninit.
    Sleep(500);

    // Invalidate icon cache.
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    Wh_Log(L"Icon cache refreshed.");

}

void LoadSettings() {

    std::unordered_map<std::wstring, std::wstring> redirections;

    auto add_redirections = [&redirections](std::wstring config_key, std::wstring target_base) {

        for(int i = 0;; i++) {

            PCWSTR bundle = Wh_GetStringSetting(L"%s[%d].bundle", config_key.c_str(), i);
            PCWSTR assets_folder = Wh_GetStringSetting(L"%s[%d].assets-folder", config_key.c_str(), i);
            PCWSTR redirect = Wh_GetStringSetting(L"%s[%d].redirect", config_key.c_str(), i);

            bool hasRedirection = *bundle && *assets_folder && *redirect;

            if(hasRedirection) {
                
                auto path = std::format(L"\\??\\{}\\{}_*\\{}", target_base, bundle, assets_folder);
                auto redirection = std::format(L"\\??\\{}", redirect); 

                redirections[path] = redirection;

            }

            Wh_FreeStringSetting(bundle);
            Wh_FreeStringSetting(assets_folder);
            Wh_FreeStringSetting(redirect);

            if(!hasRedirection) {
                break;
            }

        }

    };

    add_redirections(L"windows-apps", L"C:\\Program Files\\WindowsApps");
    add_redirections(L"system-apps", L"C:\\Windows\\SystemApps");

    g_redirections = std::move(redirections);

    Wh_Log(L"Loaded %d redirection(s).", g_redirections.size());

}

void Wh_ModInit() {

    LoadSettings();

    HMODULE ntdll = GetModuleHandle(L"ntdll.dll");

    Wh_SetFunctionHook(
        (void*)GetProcAddress(ntdll, "NtCreateFile"),
        (void*)NtCreateFile_Hook,
        (void**)&NtCreateFile_Original
    );

    RefreshIcons();

    Wh_Log(L"UWP Assets Redirect has been initialized.");

}

void Wh_ModSettingsChanged() {
    Wh_Log(L"Reloading configuration...");
    LoadSettings();
    RefreshIcons();
}

void Wh_ModUninit() {
    RefreshIcons();
}