#include "pch.h"
#include "UpdateManager.h"

#include <windows.h>
#include <winhttp.h>
#include <shlwapi.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <system_error>
#include <cwctype>
#include <cctype>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shlwapi.lib")

namespace
{
    struct HttpResponse
    {
        DWORD status = 0;
        std::string body;
    };

    constexpr wchar_t kUserAgent[] = L"NexusLauncher/1.0";

    bool DownloadHttp(const std::wstring& url, const std::wstring& outputPath)
    {
        URL_COMPONENTS uc{};
        uc.dwStructSize = sizeof(uc);
        wchar_t host[256]{};
        wchar_t path[2048]{};
        uc.lpszHostName = host;
        uc.dwHostNameLength = _countof(host);
        uc.lpszUrlPath = path;
        uc.dwUrlPathLength = _countof(path);
        uc.dwSchemeLength = 0;
        uc.dwHostNameLength = _countof(host);
        uc.dwUrlPathLength = _countof(path);

        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc))
            return false;

        HINTERNET session = WinHttpOpen(kUserAgent,
                                        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                        WINHTTP_NO_PROXY_NAME,
                                        WINHTTP_NO_PROXY_BYPASS,
                                        0);
        if (!session)
            return false;

        HINTERNET connect = WinHttpConnect(session, host, uc.nPort, 0);
        if (!connect)
        {
            WinHttpCloseHandle(session);
            return false;
        }

        DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET request = WinHttpOpenRequest(connect, L"GET", path, nullptr,
                                               WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES,
                                               flags);
        if (!request)
        {
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return false;
        }

        WinHttpAddRequestHeaders(request,
                                 L"Accept: */*\r\n",
                                 static_cast<DWORD>(-1),
                                 WINHTTP_ADDREQ_FLAG_ADD);

        bool ok = false;
        if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(request, nullptr))
        {
            DWORD status = 0;
            DWORD statusSize = sizeof(status);
            WinHttpQueryHeaders(request,
                                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX,
                                &status, &statusSize, WINHTTP_NO_HEADER_INDEX);

            if (status >= 200 && status < 300)
            {
                std::ofstream out(std::filesystem::path(outputPath), std::ios::binary | std::ios::trunc);
                if (out)
                {
                    std::vector<char> buffer(64 * 1024);
                    DWORD available = 0;
                    ok = true;
                    while (WinHttpQueryDataAvailable(request, &available) && available > 0)
                    {
                        DWORD toRead = (available < buffer.size()) ? available : static_cast<DWORD>(buffer.size());
                        DWORD read = 0;
                        if (!WinHttpReadData(request, buffer.data(), toRead, &read))
                        {
                            ok = false;
                            break;
                        }
                        if (read == 0)
                            break;
                        out.write(buffer.data(), read);
                        if (!out)
                        {
                            ok = false;
                            break;
                        }
                        available -= read;
                    }
                }
            }
        }

        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return ok;
    }

    bool HttpGetJson(const std::wstring& url, HttpResponse& response)
    {
        URL_COMPONENTS uc{};
        uc.dwStructSize = sizeof(uc);
        wchar_t host[256]{};
        wchar_t path[4096]{};
        wchar_t extra[2048]{};
        uc.lpszHostName = host;
        uc.dwHostNameLength = _countof(host);
        uc.lpszUrlPath = path;
        uc.dwUrlPathLength = _countof(path);
        uc.lpszExtraInfo = extra;
        uc.dwExtraInfoLength = _countof(extra);
        uc.dwSchemeLength = 0;
        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc))
            return false;

        HINTERNET session = WinHttpOpen(kUserAgent,
                                        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                        WINHTTP_NO_PROXY_NAME,
                                        WINHTTP_NO_PROXY_BYPASS,
                                        0);
        if (!session)
            return false;

        HINTERNET connect = WinHttpConnect(session, host, uc.nPort, 0);
        if (!connect)
        {
            WinHttpCloseHandle(session);
            return false;
        }

        std::wstring object(path);
        if (uc.lpszExtraInfo && uc.dwExtraInfoLength > 0)
            object.append(extra, uc.dwExtraInfoLength);

        DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET request = WinHttpOpenRequest(connect, L"GET", object.c_str(), nullptr,
                                               WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES,
                                               flags);
        if (!request)
        {
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return false;
        }

        const wchar_t* headers = L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
        bool ok = false;
        if (WinHttpSendRequest(request, headers, static_cast<DWORD>(-1),
                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(request, nullptr))
        {
            DWORD status = 0;
            DWORD statusSize = sizeof(status);
            WinHttpQueryHeaders(request,
                                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX,
                                &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
            response.status = status;

            std::string body;
            std::vector<char> buffer(64 * 1024);
            DWORD available = 0;
            while (WinHttpQueryDataAvailable(request, &available) && available > 0)
            {
                DWORD toRead = (available < buffer.size()) ? available : static_cast<DWORD>(buffer.size());
                DWORD read = 0;
                if (!WinHttpReadData(request, buffer.data(), toRead, &read) || read == 0)
                    break;
                body.append(buffer.data(), buffer.data() + read);
            }
            response.body = std::move(body);
            ok = (status >= 200 && status < 300);
        }

        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return ok;
    }

    bool JsonStringValue(const std::string& json, const std::string& key, std::string& value)
    {
        const std::string marker = "\"" + key + "\"";
        size_t pos = json.find(marker);
        if (pos == std::string::npos)
            return false;
        pos = json.find(':', pos + marker.size());
        if (pos == std::string::npos)
            return false;
        pos++;
        while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])))
            ++pos;
        if (pos >= json.size() || json[pos] != '\"')
            return false;
        ++pos;

        std::string out;
        while (pos < json.size())
        {
            char c = json[pos++];
            if (c == '\"')
            {
                value = std::move(out);
                return true;
            }
            if (c == '\\' && pos < json.size())
            {
                char e = json[pos++];
                switch (e)
                {
                case '\\': out.push_back('\\'); break;
                case '\"': out.push_back('\"'); break;
                case '/':  out.push_back('/'); break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                default: out.push_back(e); break;
                }
            }
            else
            {
                out.push_back(c);
            }
        }
        return false;
    }

    std::wstring Widen(const std::string& s)
    {
        if (s.empty())
            return {};
        int needed = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        if (needed <= 0)
            return {};
        std::wstring out(needed, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), needed);
        return out;
    }

    bool GetLatestReleaseTag(const wchar_t* repo, std::wstring& tag)
    {
        std::wstring url = L"https://api.github.com/repos/" + std::wstring(repo) + L"/releases/latest";
        HttpResponse r;
        if (!HttpGetJson(url, r))
            return false;
        std::string value;
        if (!JsonStringValue(r.body, "tag_name", value))
            return false;
        tag = Widen(value);
        return !tag.empty();
    }

    bool GetLatestTag(const wchar_t* repo, std::wstring& tag)
    {
        std::wstring url = L"https://api.github.com/repos/" + std::wstring(repo) + L"/tags?per_page=1";
        HttpResponse r;
        if (!HttpGetJson(url, r))
            return false;
        size_t namePos = r.body.find("\"name\"");
        if (namePos == std::string::npos)
            return false;
        size_t colon = r.body.find(':', namePos);
        size_t quote = (colon == std::string::npos) ? std::string::npos : r.body.find('"', colon);
        if (quote == std::string::npos)
            return false;
        size_t end = r.body.find('"', quote + 1);
        if (end == std::string::npos)
            return false;
        tag = Widen(r.body.substr(quote + 1, end - quote - 1));
        return !tag.empty();
    }

    bool SameVersion(const std::wstring& a, const std::wstring& b)
    {
        if (a.empty() || b.empty())
            return false;
        return _wcsicmp(a.c_str(), b.c_str()) == 0;
    }

    std::wstring EscapePowerShellLiteral(const std::wstring& value)
    {
        std::wstring escaped;
        escaped.reserve(value.size());
        for (wchar_t c : value)
        {
            if (c == L'\'')
                escaped += L"''";
            else
                escaped += c;
        }
        return escaped;
    }

    bool ExtractZip(const std::wstring& zipPath, const std::wstring& destination)
    {
        // Windows 10/11 ships PowerShell's Expand-Archive, which lets the
        // launcher extract GitHub release archives without bundling a ZIP DLL.
        std::wstring command = L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"Expand-Archive -LiteralPath '";
        command += EscapePowerShellLiteral(zipPath);
        command += L"' -DestinationPath '";
        command += EscapePowerShellLiteral(destination);
        command += L"' -Force\"";

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        std::vector<wchar_t> cmd(command.begin(), command.end());
        cmd.push_back(L'\0');

        if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
            return false;

        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD exitCode = 1;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return exitCode == 0;
    }

    std::wstring TempFile(const wchar_t* name)
    {
        wchar_t tempPath[MAX_PATH]{};
        DWORD len = GetTempPathW(_countof(tempPath), tempPath);
        if (len == 0 || len >= _countof(tempPath))
            return {};
        std::filesystem::path dir(tempPath);
        dir /= L"NexusLauncher";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        dir /= name;
        return dir.wstring();
    }

    bool ReplaceFile(const std::wstring& source, const std::wstring& destination)
    {
        return MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) != FALSE;
    }
}

namespace NexusUpdater
{
    NexusUpdateResult CheckAndInstall(int gameIndex,
                                      const std::wstring& installPath,
                                      const std::wstring& currentVersion1,
                                      const std::wstring& currentVersion2)
    {
        NexusUpdateResult result;
        if (installPath.empty())
        {
            result.message = L"Set the installation folder first.";
            return result;
        }

        std::filesystem::path install(installPath);
        std::error_code ec;
        if (!std::filesystem::exists(install, ec) || !std::filesystem::is_directory(install, ec))
        {
            result.message = L"The selected installation folder does not exist.";
            return result;
        }

        auto finishFailure = [&](const std::wstring& message)
        {
            result.success = false;
            result.message = message;
            return result;
        };

        if (gameIndex == 0 || gameIndex == 1)
        {
            // CBServers' archived S1x/IW6x repositories point their README
            // downloads at the updater repo rather than GitHub Release assets.
            // Use the repository's newest tag as the version marker, then pull
            // the same latest Windows client executable linked by the repo.
            const wchar_t* repo = (gameIndex == 0) ? L"CBServers/s1x-client" : L"CBServers/iw6x-client";
            const wchar_t* exe = (gameIndex == 0) ? L"s1x.exe" : L"iw6x.exe";
            const wchar_t* updaterUrl = (gameIndex == 0)
                ? L"https://github.com/CBServers/updater/raw/main/updater/s1x/s1x.exe"
                : L"https://github.com/CBServers/updater/raw/main/updater/iw6x/iw6x.exe";

            std::wstring latest;
            if (!GetLatestReleaseTag(repo, latest))
            {
                if (!GetLatestTag(repo, latest))
                    return finishFailure(L"Nexus Launcher could not reach GitHub to check for the latest version of the client.");
            }
            result.version1 = latest;

            if (SameVersion(currentVersion1, latest))
            {
                result.success = true;
                result.updated = false;
                result.version1Installed = true;
                result.message = std::wstring(exe) + L" is already up to date (" + latest + L").";
                return result;
            }

            std::wstring temp = TempFile(exe);
            if (temp.empty() || !DownloadHttp(updaterUrl, temp))
                return finishFailure(L"The latest client could not be downloaded from GitHub.");

            std::wstring destination = (install / exe).wstring();
            if (!ReplaceFile(temp, destination))
            {
                DeleteFileW(temp.c_str());
                return finishFailure(std::wstring(L"Nexus Launcher downloaded the update, but could not replace ") + exe + L". Close the game and try again.");
            }

            result.success = true;
            result.updated = true;
            result.version1Installed = true;
            result.message = std::wstring(exe) + L" updated to " + latest + L".";
            return result;
        }

        // IW4x client: update the DLL from the latest iw4x-client release.
        std::wstring clientTag;
        if (!GetLatestReleaseTag(L"iw4x/iw4x-client", clientTag))
            return finishFailure(L"Nexus Launcher could not find the latest IW4x client release on GitHub.");

        std::wstring rawTag;
        if (!GetLatestReleaseTag(L"iw4x/iw4x-rawfiles", rawTag))
            return finishFailure(L"Nexus Launcher could not find the latest IW4x rawfiles release on GitHub.");

        result.version1 = clientTag;
        result.version2 = rawTag;

        bool clientNeedsUpdate = !SameVersion(currentVersion1, clientTag);
        bool rawNeedsUpdate = !SameVersion(currentVersion2, rawTag);

        if (!clientNeedsUpdate && !rawNeedsUpdate)
        {
            result.success = true;
            result.updated = false;
            result.version1Installed = true;
            result.version2Installed = true;
            result.message = L"IW4x client (" + clientTag + L") and rawfiles (" + rawTag + L") are already up to date.";
            return result;
        }

        if (clientNeedsUpdate)
        {
            std::wstring tempDll = TempFile(L"iw4x.dll");
            if (tempDll.empty() || !DownloadHttp(L"https://github.com/iw4x/iw4x-client/releases/latest/download/iw4x.dll", tempDll))
                return finishFailure(L"The latest IW4x DLL could not be downloaded from GitHub.");

            std::wstring dllDestination = (install / L"iw4x.dll").wstring();
            if (!ReplaceFile(tempDll, dllDestination))
            {
                DeleteFileW(tempDll.c_str());
                return finishFailure(L"The IW4x DLL was downloaded but could not be installed. Close IW4x and try again.");
            }
            result.version1Installed = true;
        }
        else
        {
            result.version1Installed = true;
        }

        if (rawNeedsUpdate)
        {
            std::wstring tempZip = TempFile(L"iw4x-rawfiles-release.zip");
            if (tempZip.empty() || !DownloadHttp(L"https://github.com/iw4x/iw4x-rawfiles/releases/latest/download/release.zip", tempZip))
            {
                DeleteFileW(tempZip.c_str());
                return finishFailure(L"The latest IW4x rawfiles release.zip could not be downloaded from GitHub.");
            }

            if (!ExtractZip(tempZip, installPath))
            {
                DeleteFileW(tempZip.c_str());
                return finishFailure(L"The IW4x rawfiles archive downloaded successfully, but extraction failed.");
            }
            DeleteFileW(tempZip.c_str());
            result.version2Installed = true;
        }
        else
        {
            result.version2Installed = true;
        }

        result.success = true;
        result.updated = true;
        if (clientNeedsUpdate && rawNeedsUpdate)
            result.message = L"IW4x updated: client " + clientTag + L" and rawfiles " + rawTag + L".";
        else if (clientNeedsUpdate)
            result.message = L"IW4x client DLL updated to " + clientTag + L". Rawfiles are already current.";
        else
            result.message = L"IW4x rawfiles updated to " + rawTag + L". The client DLL is already current.";
        return result;
    }
}
