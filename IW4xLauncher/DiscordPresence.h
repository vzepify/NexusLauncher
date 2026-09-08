#pragma once

#include <windows.h>
#include <string>
#include <cstdint>

// Lightweight Discord Rich Presence client implemented directly over
// Discord's native Windows IPC pipe. No Discord DLL or third-party library
// is required. The Discord application/client ID is compiled into the launcher.
// Note: a Discord Client ID is not a secret and can be distributed with the app.
class CDiscordPresence
{
public:
    CDiscordPresence() = default;
    ~CDiscordPresence();

    bool Initialize(const std::wstring& clientId);
    void Shutdown();

    // Publishes the current Nexus Launcher activity. The caller keeps the
    // same start timestamp while the detected game process remains the same.
    bool Update(const std::wstring& gameName,
                const std::wstring& serverName,
                std::int64_t startUnixSeconds,
                DWORD gamePid);

    // Publishes a Nexus Launcher activity while no supported game is running.
    bool UpdateLauncher(std::int64_t startUnixSeconds);

    bool IsConnected() const { return m_pipe != INVALID_HANDLE_VALUE; }

private:
    bool Connect();
    bool SendFrame(std::uint32_t opcode, const std::string& payload);
    bool WriteAll(const void* data, DWORD size);
    bool ReadFrame(std::string& payload, DWORD timeoutMs);

    static std::string Utf8(const std::wstring& value);
    static std::string JsonEscape(const std::string& value);
    static std::string MakeNonce();

    HANDLE m_pipe = INVALID_HANDLE_VALUE;
    std::wstring m_clientId;
};
