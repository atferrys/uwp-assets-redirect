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
  - - bundle: ""
      $name: Bundle name
      $description: >-
        The application bundle without any version appended.

        You can get easily get this via Task Manager, follow the guide in
        the details tab for more information.
    - redirect: ""
      $name: Redirection folder
      $description: The folder with the custom assets files.
  $name: WindowsApps Redirections
  $description: >-
    Redirections for the apps found in "C:\Program Files\WindowsApps".

    This is used for Microsoft Store apps and some system apps.

- system-apps:
  - - bundle: ""
      $name: Bundle name
      $description: >-
        The application bundle without any version appended.

        You can get easily get this via Task Manager, follow the guide in
        the details tab for more information.
    - redirect: ""
      $name: Redirection folder
      $description: The folder with the custom assets files.
  $name: SystemApps Redirections
  $description: >-
    Redirections for the apps found in "C:\Windows\SystemApps".

    This is used for some system apps like "Wireless Display".
- custom:
  - - assets-path: ""
      $name: Original path
      $description: >-
        The full application assets path.

        Can be a pattern where '*' matches any number of characters.
    - redirect: ""
      $name: Redirection folder
      $description: The folder with the custom assets files.
  $name: ⚠️ CAUTION - Custom Redirections
  $description: >-
    Redirections for system apps that aren't found in any of the previous folders.

    This can be used for apps like "Settings".

    You'll have to specify the assets folder in the path this time and not
    simply the app bundle.
*/
// ==/WindhawkModSettings==

#include <windhawk_utils.h>
#include <windows.h>
#include <winternl.h>
#include <shlobj.h>
#include <format>
#include <string>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <vector>

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

std::wstring get_assets_folder(const std::wstring& appx_manifest) {

    std::wifstream file(appx_manifest.c_str());

    if (!file.is_open()) {
        Wh_Log(L"Failed to open manifest: %s", appx_manifest.c_str());
        return L"";
    }

    std::wstringstream buffer;
    buffer << file.rdbuf();

    std::wstring xml = buffer.str();

    const std::wstring openTag = L"<Logo>";
    const std::wstring closeTag = L"</Logo>";

    size_t start = xml.find(openTag);

    if (start == std::wstring::npos) {
        Wh_Log(L"Failed to find opening <Logo> tag: %s", appx_manifest.c_str());
        return L"";
    }

    start += openTag.length();
    size_t end = xml.find(closeTag, start);

    if (end == std::wstring::npos) {
        Wh_Log(L"Failed to find closing <Logo> tag: %s", appx_manifest.c_str());
        return L"";
    }

    std::wstring logo_content = xml.substr(start, end - start);
    size_t backslash_position = logo_content.find_last_of('\\');

    // If assets are in root directory
    if (backslash_position == std::wstring::npos) {
        Wh_Log(L"Failed to find external assets folder, assets are in the root directory: %s", appx_manifest.c_str());
        return L"";
    }

    return logo_content.substr(0, backslash_position);

}

std::wstring find_bundle_folder(const std::wstring& apps_directory, const std::wstring& app_bundle) {

    for (const auto& entry : std::filesystem::directory_iterator(apps_directory)) {

        if (!entry.is_directory()) {
            continue;
        }

        std::wstring path = entry.path().wstring();

        size_t last_backslash_index = path.find_last_of('\\');
        size_t version_index = path.find_first_of('_', last_backslash_index);

        if (last_backslash_index == std::string::npos || version_index == std::string::npos) {
            continue;
        }

        if (path.substr(last_backslash_index + 1, version_index - last_backslash_index - 1) != app_bundle) {
            continue;
        }

        if (path.find(L"neutral", version_index) != std::string::npos) {
            continue;
        }

        return path;

    }

    return L"";
}

const std::wstring g_redirections_cache_size_key = L"redirections_cache_size";
const std::wstring g_redirections_cache_key = L"redirections_cache";

const std::wstring g_default_assets_folder = L"Assets";
const int g_max_read_tries = 20;

void write_redirections_cache(std::unordered_map<std::wstring, std::wstring>& redirections) {

    std::vector<uint8_t> buffer;

    auto write = [&](const void* data, size_t size) {
        size_t oldSize = buffer.size();
        buffer.resize(oldSize + size);
        memcpy(buffer.data() + oldSize, data, size);
    };

    // number of entries
    uint32_t count = (uint32_t) redirections.size();
    write(&count, sizeof(count));

    for (const auto& [key, value] : redirections) {
        uint32_t keyLen = (uint32_t) key.size();
        write(&keyLen, sizeof(keyLen));
        write(key.data(), keyLen * sizeof(wchar_t));

        uint32_t valueLen = (uint32_t) value.size();
        write(&valueLen, sizeof(valueLen));
        write(value.data(), valueLen * sizeof(wchar_t));
    }

    Wh_SetBinaryValue(g_redirections_cache_key.c_str(), buffer.data(), buffer.size());
    Wh_SetIntValue(g_redirections_cache_size_key.c_str(), buffer.size());

}

bool read_redirections_cache(std::unordered_map<std::wstring, std::wstring>& redirections) {

    std::vector<uint8_t> buffer;

    int buffer_size = Wh_GetIntValue(g_redirections_cache_size_key.c_str(), -1);

    if(buffer_size == -1) {
        return false;
    }

    buffer.resize(buffer_size);

    if(Wh_GetBinaryValue(g_redirections_cache_key.c_str(), buffer.data(), buffer.size()) == 0) {
        return false;
    }

    const uint8_t* ptr = buffer.data();

    auto read = [&](void* out, size_t size) {
        memcpy(out, ptr, size);
        ptr += size;
    };

    uint32_t count;
    read(&count, sizeof(count));

    for (uint32_t i = 0; i < count; i++) {
        uint32_t keyLen;
        read(&keyLen, sizeof(keyLen));

        std::wstring key(keyLen, L'\0');
        read(key.data(), keyLen * sizeof(wchar_t));

        uint32_t valueLen;
        read(&valueLen, sizeof(valueLen));

        std::wstring value(valueLen, L'\0');
        read(value.data(), valueLen * sizeof(wchar_t));

        redirections.emplace(std::move(key), std::move(value));
    }

    return true;

}

void ClearRedirectionsCache(bool check_main = true) {
    if(!check_main || DoesCurrentProcessOwnTaskbar()) {
        Wh_DeleteValue(g_redirections_cache_size_key.c_str());
        Wh_DeleteValue(g_redirections_cache_key.c_str());
    }
}

void LoadRedirections(std::unordered_map<std::wstring, std::wstring>& redirections) {

    auto normalize_path = [](std::wstring path, std::wstring base_path = L"C:\\Windows\\System32\\") -> std::wstring {

        // ### Expand environment strings like %ProgramFiles%

        DWORD path_size = ExpandEnvironmentStringsW(path.c_str(), nullptr, 0);

        if (path_size == 0) {
            return L"";
        }

        std::wstring expanded_path(path_size, L'\0');
        ExpandEnvironmentStringsW(path.c_str(), &expanded_path[0], path_size);

        // Remove trailing null character
        if (!expanded_path.empty() && expanded_path.back() == L'\0') {
            expanded_path.pop_back();
        }

        // ### Make path absolute if relative path like ".\Redirection" is given

        std::filesystem::path relative_path(expanded_path);

        if (relative_path.is_absolute()) {
            return std::filesystem::weakly_canonical(relative_path).wstring();
        }

        std::filesystem::path relative_prefixed_path = std::filesystem::path(base_path) / relative_path;
        std::wstring absolute_path = std::filesystem::weakly_canonical(relative_prefixed_path).wstring();

        // ### Remove trailing slashes

        while (!absolute_path.empty() && absolute_path.back() == L'\\') {

            size_t path_size = absolute_path.size();

            // Stop if it's a drive root like "C:\"
            if (path_size == 3 && absolute_path[path_size - 2] == L':' && absolute_path[path_size - 1] == L'\\') {
                break;
            }

            absolute_path.pop_back();

        }

        return absolute_path;

    };

    auto add_bundle_redirections = [&redirections, normalize_path](std::wstring config_key, std::wstring target_base) {

        auto deduct_bundle = [target_base](std::wstring bundle, std::wstring& bundle_id, std::wstring& assets_folder) {

            const auto trim = [](std::wstring string) -> std::wstring {

                const auto should_trim = [](wchar_t character) {
                    return std::iswspace(character) || character == L'\\';
                };

                auto start = std::find_if_not(string.begin(), string.end(), should_trim);
                auto end = std::find_if_not(string.rbegin(), string.rend(), should_trim).base();

                if (start >= end) {
                    return L"";
                }

                return std::wstring(start, end);

            };

            size_t separator_index = bundle.find('|');

            if(bundle.find('\\') < (separator_index == std::wstring::npos ? bundle.size() : separator_index)) {
                Wh_Log(L"Invalid bundle name for \"%s\".", bundle.c_str());
                return;
            }

            if(separator_index != std::wstring::npos) {

                bundle_id = trim(bundle.substr(0, separator_index));
                assets_folder = trim(bundle.substr(separator_index + 1));

                if(assets_folder.find(L".\\") != std::wstring::npos ||
                   assets_folder.find(L"\\.") != std::wstring::npos ||
                   assets_folder == L"..") {
                    assets_folder = g_default_assets_folder;
                    Wh_Log(L"Invalid assets folder for \"%s\", falling back to default.", bundle_id.c_str());
                }

                return;
            }

            bundle_id = trim(bundle);
            std::wstring bundle_folder = find_bundle_folder(target_base, bundle_id);

            if(bundle_folder.empty()) {
                assets_folder = g_default_assets_folder;
                Wh_Log(L"Failed to find bundle folder for \"%s\", falling back to default assets folder.", bundle_id.c_str());
                return;
            }

            assets_folder = get_assets_folder(std::format(L"{}\\AppxManifest.xml", bundle_folder));

            if(assets_folder.empty()) {
                assets_folder = g_default_assets_folder;
                Wh_Log(L"Failed to get assets folder for \"%s\" automatically, falling back to default.", bundle_id.c_str());
                return;
            }

            Wh_Log(L"Automatically found assets folder for \"%s\" in \"%s\".", bundle_id.c_str(), assets_folder.c_str());

        };

        // Load from settings

        for(int i = 0;; i++) {

            PCWSTR bundle = Wh_GetStringSetting(L"%s[%d].bundle", config_key.c_str(), i);
            PCWSTR redirect = Wh_GetStringSetting(L"%s[%d].redirect", config_key.c_str(), i);

            bool hasRedirection = *bundle && *redirect;

            if(hasRedirection) {

                std::wstring bundle_id;
                std::wstring assets_folder;

                deduct_bundle(std::wstring(bundle), bundle_id, assets_folder);

                if(bundle_id.empty() || assets_folder.empty()) {
                    continue;
                }

                auto path = std::format(L"\\??\\{}\\{}_*\\{}", target_base, bundle_id, assets_folder);
                auto redirection = std::format(L"\\??\\{}", normalize_path(redirect));

                redirections[path] = redirection;

            }

            Wh_FreeStringSetting(bundle);
            Wh_FreeStringSetting(redirect);

            if(!hasRedirection) {
                break;
            }

        }

    };

    auto add_custom_redirections = [&redirections, normalize_path](std::wstring config_key) {

        // Load from settings

        for(int i = 0;; i++) {

            PCWSTR assets_path = Wh_GetStringSetting(L"%s[%d].assets-path", config_key.c_str(), i);
            PCWSTR redirect = Wh_GetStringSetting(L"%s[%d].redirect", config_key.c_str(), i);

            bool hasRedirection = *assets_path && *redirect;

            if(hasRedirection) {

                auto path = std::format(L"\\??\\{}", normalize_path(assets_path));
                auto redirection = std::format(L"\\??\\{}", normalize_path(redirect));

                redirections[path] = redirection;

            }

            Wh_FreeStringSetting(assets_path);
            Wh_FreeStringSetting(redirect);

            if(!hasRedirection) {
                break;
            }

        }

    };

    add_bundle_redirections(L"windows-apps", L"C:\\Program Files\\WindowsApps");
    add_bundle_redirections(L"system-apps", L"C:\\Windows\\SystemApps");

    add_custom_redirections(L"custom");

}

void LoadSettings() {

    std::unordered_map<std::wstring, std::wstring> redirections;

    auto load_and_cache = [&redirections]() {

        ClearRedirectionsCache(false);
        LoadRedirections(redirections);
        write_redirections_cache(redirections);

        Wh_Log(L"Loaded %i redirections and stored them to shared cache.", redirections.size());
        g_redirections = std::move(redirections);

    };

    if(DoesCurrentProcessOwnTaskbar()) {
        load_and_cache();
        return;
    }

    int tries = 0;

    while(tries++ < g_max_read_tries && !read_redirections_cache(redirections)) {
        Wh_Log(L"Redirections haven't been cached yet, retrying... (%i/%i)", tries, g_max_read_tries);
        Sleep(100);
    }

    if(tries > g_max_read_tries) {
        Wh_Log(L"Redirections cache read tries exceeded, loading redirections anyways.");
        load_and_cache();
        return;
    }

    Wh_Log(L"Loaded %i redirections from shared cache.", redirections.size());
    g_redirections = std::move(redirections);

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

BOOL Wh_ModSettingsChanged(BOOL* bReload) {

    Wh_Log(L"Reloading configuration...");

    if(!IsExplorerProcess()) {
        *bReload = TRUE;
        return TRUE;
    }

    LoadSettings();
    RefreshIcons();

    return TRUE;

}

void Wh_ModUninit() {
    ClearRedirectionsCache();
    RefreshIcons();
}