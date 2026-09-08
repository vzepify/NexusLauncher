#pragma once

#include <string>

struct NexusUpdateResult
{
    bool success = false;
    bool updated = false;
    std::wstring version1;
    std::wstring version2;
    bool version1Installed = false;
    bool version2Installed = false;
    std::wstring message;
};

namespace NexusUpdater
{
    NexusUpdateResult CheckAndInstall(int gameIndex,
                                      const std::wstring& installPath,
                                      const std::wstring& currentVersion1,
                                      const std::wstring& currentVersion2);
}
