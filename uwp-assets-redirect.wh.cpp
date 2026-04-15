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
#include <string>
#include <unordered_map>

static const std::wstring g_TargetBase =
    L"\\??\\C:\\Program Files\\WindowsApps";

static const std::wstring g_TargetBaseSys =
    L"\\??\\C:\\Windows\\SystemApps";

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

    std::wstring originalPath;

    if(ObjectAttributes && ObjectAttributes->ObjectName) {
        UNICODE_STRING* name = ObjectAttributes->ObjectName;
        originalPath.assign(name->Buffer, name->Length / sizeof(WCHAR));
    }

    // Check if we should redirect
    if(!originalPath.empty()) {

        std::wstring redirectPath;

        const wchar_t* path = originalPath.c_str();
        size_t pathLength = originalPath.length();

        for(const auto& pair : g_redirections) {
            
            const wchar_t* pattern = pair.first.c_str();
            size_t patternLength = pair.first.length();

            size_t match_end_index = 0;

            if(match(pattern, patternLength, path, pathLength, match_end_index)) {
                redirectPath = pair.second + originalPath.substr(match_end_index);
                break;
            }

        }

        if(!redirectPath.empty()) {

            Wh_Log(L"[Redirect Attempt] %s -> %s", originalPath.c_str(), redirectPath.c_str());

            UNICODE_STRING redirectName {};
            redirectName.Buffer = (PWSTR) redirectPath.c_str();
            redirectName.Length = (USHORT) (redirectPath.length() * sizeof(WCHAR));
            redirectName.MaximumLength = redirectName.Length;

            ObjectAttributes->ObjectName = &redirectName;

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

            UNICODE_STRING originalName {};
            originalName.Buffer = (PWSTR) originalPath.c_str();
            originalName.Length = (USHORT) (originalPath.length() * sizeof(WCHAR));
            originalName.MaximumLength = originalName.Length;

            ObjectAttributes->ObjectName = &originalName;

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

void LoadSettings() {

    std::unordered_map<std::wstring, std::wstring> redirections;

    for(int i = 0;; i++) {

        PCWSTR bundle = Wh_GetStringSetting(L"windows-apps[%d].bundle", i);
        PCWSTR assetsFolder = Wh_GetStringSetting(L"windows-apps[%d].assets-folder", i);
        PCWSTR redirect = Wh_GetStringSetting(L"windows-apps[%d].redirect", i);

        bool hasRedirection = *bundle || *redirect;

        if(hasRedirection) {

            std::wstring bundleStr(bundle);
            std::wstring assetsFolderStr(assetsFolder);
            std::wstring redirectStr(redirect);

            redirections[g_TargetBase + L"\\" + bundleStr + L"_*\\" + assetsFolderStr] = L"\\??\\" + redirectStr;

        }

        Wh_FreeStringSetting(bundle);
        Wh_FreeStringSetting(redirect);

        if(!hasRedirection) {
            break;
        }

    }

    for(int i = 0;; i++) {

        PCWSTR bundle = Wh_GetStringSetting(L"system-apps[%d].bundle", i);
        PCWSTR assetsFolder = Wh_GetStringSetting(L"system-apps[%d].assets-folder", i);
        PCWSTR redirect = Wh_GetStringSetting(L"system-apps[%d].redirect", i);

        bool hasRedirection = *bundle || *redirect;

        if(hasRedirection) {

            std::wstring bundleStr(bundle);
            std::wstring assetsFolderStr(assetsFolder);
            std::wstring redirectStr(redirect);

            redirections[g_TargetBaseSys + L"\\" + bundleStr + L"_*\\" + assetsFolderStr] = L"\\??\\" + redirectStr;

        }

        Wh_FreeStringSetting(bundle);
        Wh_FreeStringSetting(redirect);

        if(!hasRedirection) {
            break;
        }

    }

    g_redirections = std::move(redirections);

    Wh_Log(L"Configuration loaded.");

}

void Wh_ModInit() {

    LoadSettings();

    HMODULE ntdll = GetModuleHandle(L"ntdll.dll");

    Wh_SetFunctionHook(
        (void*)GetProcAddress(ntdll, "NtCreateFile"),
        (void*)NtCreateFile_Hook,
        (void**)&NtCreateFile_Original
    );

    Wh_Log(L"UWP Assets Redirect has been initialized.");
    
}

void Wh_ModSettingsChanged() {
    Wh_Log(L"Reloading configuration...");
    LoadSettings();
}