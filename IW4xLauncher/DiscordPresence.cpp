#include "pch.h"
#include "DiscordPresence.h"

#include <windows.h>
#include <rpc.h>
#include <sstream>

#pragma comment(lib, "Rpcrt4.lib")

namespace
{
    constexpr std::uint32_t OP_HANDSHAKE = 0;
    constexpr std::uint32_t OP_FRAME = 1;

    bool IsPipeOpen(HANDLE h)
    {
        return h != nullptr && h != INVALID_HANDLE_VALUE;
    }
}

CDiscordPresence::~CDiscordPresence()
{
    Shutdown();
}

bool CDiscordPresence::Initialize(const std::wstring& clientId)
{
    Shutdown();
    m_clientId = clientId;
    if (m_clientId.empty())
        return false;

    return Connect();
}

void CDiscordPresence::Shutdown()
{
    if (IsPipeOpen(m_pipe))
    {
        // Best effort clear. Discord will also remove the activity when the
        // IPC connection disappears, so failure here is harmless.
        const std::string payload =
            "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" +
            std::to_string(GetCurrentProcessId()) +
            ",\"activity\":null},\"nonce\":\"clear\"}";
        SendFrame(OP_FRAME, payload);
        FlushFileBuffers(m_pipe);
        CloseHandle(m_pipe);
        m_pipe = INVALID_HANDLE_VALUE;
    }

    m_clientId.clear();
}

bool CDiscordPresence::Connect()
{
    if (m_clientId.empty())
        return false;

    for (int i = 0; i < 10; ++i)
    {
        wchar_t pipeName[64] = {};
        swprintf_s(pipeName, L"\\\\?\\pipe\\discord-ipc-%d", i);

        HANDLE h = CreateFileW(pipeName,
                               GENERIC_READ | GENERIC_WRITE,
                               0,
                               nullptr,
                               OPEN_EXISTING,
                               FILE_FLAG_WRITE_THROUGH,
                               nullptr);
        if (!IsPipeOpen(h))
            continue;

        m_pipe = h;

        const std::string hello =
            "{\"v\":1,\"client_id\":\"" +
            JsonEscape(Utf8(m_clientId)) + "\"}";

        if (!SendFrame(OP_HANDSHAKE, hello))
        {
            CloseHandle(m_pipe);
            m_pipe = INVALID_HANDLE_VALUE;
            continue;
        }

        // Discord sends a READY frame immediately after a successful
        // handshake. It is not required to construct a basic presence, but
        // consuming it prevents the first command from reading stale data.
        std::string ignored;
        ReadFrame(ignored, 250);
        return true;
    }

    return false;
}

bool CDiscordPresence::WriteAll(const void* data, DWORD size)
{
    if (!IsPipeOpen(m_pipe))
        return false;

    const BYTE* bytes = static_cast<const BYTE*>(data);
    DWORD total = 0;
    while (total < size)
    {
        DWORD written = 0;
        if (!WriteFile(m_pipe, bytes + total, size - total, &written, nullptr) || written == 0)
            return false;
        total += written;
    }
    return true;
}

bool CDiscordPresence::SendFrame(std::uint32_t opcode, const std::string& payload)
{
    if (!IsPipeOpen(m_pipe))
        return false;

    const std::uint32_t length = static_cast<std::uint32_t>(payload.size());
    if (!WriteAll(&opcode, sizeof(opcode)))
        return false;
    if (!WriteAll(&length, sizeof(length)))
        return false;
    if (!payload.empty() && !WriteAll(payload.data(), length))
        return false;

    return true;
}

bool CDiscordPresence::ReadFrame(std::string& payload, DWORD timeoutMs)
{
    payload.clear();
    if (!IsPipeOpen(m_pipe))
        return false;

    const auto start = GetTickCount64();
    std::uint32_t opcode = 0;
    std::uint32_t length = 0;

    auto readExact = [&](void* out, DWORD bytes) -> bool
    {
        BYTE* dst = static_cast<BYTE*>(out);
        DWORD done = 0;
        while (done < bytes)
        {
            DWORD available = 0;
            if (!PeekNamedPipe(m_pipe, nullptr, 0, nullptr, &available, nullptr))
                return false;
            if (available == 0)
            {
                if (GetTickCount64() - start >= timeoutMs)
                    return false;
                Sleep(5);
                continue;
            }

            DWORD got = 0;
            if (!ReadFile(m_pipe, dst + done, bytes - done, &got, nullptr) || got == 0)
                return false;
            done += got;
        }
        return true;
    };

    if (!readExact(&opcode, sizeof(opcode)) || !readExact(&length, sizeof(length)))
        return false;
    if (length > 1024 * 1024)
        return false;

    payload.resize(length);
    if (length && !readExact(payload.data(), length))
    {
        payload.clear();
        return false;
    }
    return true;
}

bool CDiscordPresence::UpdateLauncher(std::int64_t startUnixSeconds)
{
    if (m_clientId.empty())
        return false;

    if (!IsConnected() && !Connect())
        return false;

    const std::string details = "Using Nexus Launcher";
    const std::string state = "Launcher Open";
    const std::string nonce = MakeNonce();
    const std::uint32_t pid = GetCurrentProcessId();

    std::ostringstream json;
    json << "{\"cmd\":\"SET_ACTIVITY\",\"args\":{";
    json << "\"pid\":" << pid << ",";
    json << "\"activity\":{";
    json << "\"type\":0,";
    json << "\"details\":\"" << JsonEscape(details) << "\",";
    json << "\"state\":\"" << JsonEscape(state) << "\",";
    json << "\"timestamps\":{";
    json << "\"start\":" << startUnixSeconds;
    json << "},";
    json << "\"assets\":{";
    json << "\"large_image\":\"nexus_launcher\",";
    json << "\"large_text\":\"Nexus Launcher\"";
    json << "},";
    json << "\"instance\":true";
    json << "}},\"nonce\":\"" << nonce << "\"}";

    if (SendFrame(OP_FRAME, json.str()))
        return true;

    if (IsPipeOpen(m_pipe))
    {
        CloseHandle(m_pipe);
        m_pipe = INVALID_HANDLE_VALUE;
    }

    return Connect() && SendFrame(OP_FRAME, json.str());
}

bool CDiscordPresence::Update(const std::wstring& gameName,
                              const std::wstring& serverName,
                              std::int64_t startUnixSeconds,
                              DWORD gamePid)
{
    if (m_clientId.empty())
        return false;

    if (!IsConnected() && !Connect())
        return false;

    std::wstring stateWide = serverName.empty() ? L"In Game" : serverName;
    if (stateWide.size() > 128)
        stateWide.resize(128);

    const std::string details = "Playing " + Utf8(gameName) + " with Nexus Launcher";
    const std::string state = Utf8(stateWide);
    const std::string nonce = MakeNonce();

    // Use the Nexus Launcher application as the Discord activity identity.
    // Large image keys are expected to be uploaded in the Discord Developer
    // Portal for the same application: nexus_s1x / nexus_iw6x / nexus_iw4x.
    std::string imageKey = "nexus_launcher";
    if (_wcsicmp(gameName.c_str(), L"S1X") == 0)
        imageKey = "nexus_s1x";
    else if (_wcsicmp(gameName.c_str(), L"IW6X") == 0)
        imageKey = "nexus_iw6x";
    else if (_wcsicmp(gameName.c_str(), L"IW4X") == 0)
        imageKey = "nexus_iw4x";

    const std::uint32_t pid = GetCurrentProcessId();
    const std::uint32_t actualGamePid = gamePid;
    (void)actualGamePid; // Kept for future join integration; RPC pid is launcher PID.

    std::ostringstream json;
    json << "{\"cmd\":\"SET_ACTIVITY\",\"args\":{";
    json << "\"pid\":" << pid << ",";
    json << "\"activity\":{";
    json << "\"type\":0,";
    json << "\"details\":\"" << JsonEscape(details) << "\",";
    json << "\"state\":\"" << JsonEscape(state) << "\",";
    json << "\"timestamps\":{";
    json << "\"start\":" << startUnixSeconds;
    json << "},";
    json << "\"assets\":{";
    json << "\"large_image\":\"" << JsonEscape(imageKey) << "\",";
    json << "\"large_text\":\"Nexus Launcher\"";
    json << "},";
    json << "\"instance\":true";
    json << "}},\"nonce\":\"" << nonce << "\"}";

    if (SendFrame(OP_FRAME, json.str()))
        return true;

    // Discord may have restarted its IPC endpoint. Drop the handle and try
    // one clean reconnect so presence recovers without restarting Nexus.
    if (IsPipeOpen(m_pipe))
    {
        CloseHandle(m_pipe);
        m_pipe = INVALID_HANDLE_VALUE;
    }
    return Connect() && SendFrame(OP_FRAME, json.str());
}

std::string CDiscordPresence::Utf8(const std::wstring& value)
{
    if (value.empty())
        return {};

    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), (int)value.size(), nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return {};

    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), (int)value.size(), result.data(), size, nullptr, nullptr);
    return result;
}

std::string CDiscordPresence::JsonEscape(const std::string& value)
{
    std::string out;
    out.reserve(value.size() + 8);
    for (unsigned char c : value)
    {
        switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20)
            {
                char buf[7] = {};
                sprintf_s(buf, "\\u%04x", c);
                out += buf;
            }
            else
            {
                out.push_back((char)c);
            }
            break;
        }
    }
    return out;
}

std::string CDiscordPresence::MakeNonce()
{
    UUID uuid{};
    RPC_STATUS status = UuidCreate(&uuid);
    if (status != RPC_S_OK && status != RPC_S_UUID_LOCAL_ONLY)
        return std::to_string(GetTickCount64());

    RPC_CSTR str = nullptr;
    if (UuidToStringA(&uuid, &str) != RPC_S_OK || str == nullptr)
        return std::to_string(GetTickCount64());

    std::string result(reinterpret_cast<char*>(str));
    RpcStringFreeA(&str);
    return result;
}
