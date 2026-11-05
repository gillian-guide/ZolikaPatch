module;

#include <common.hxx>

export module launcher;

import bytes;
import common;
import game;
import loader;
import pattern;

// LaunchWithPlayGTAIV: the two launch slots carry `LaunchGTAIV.exe\0`; the mod
// rewrites both to `PlayGTAIV.exe\0e\0` so the game relaunches through the DRM
// bypass. The scan is wildcard-free and hits exactly the two slots on the five
// builds the original gated, and unrelated slots on the builds it excluded - so
// the gate is load-bearing, not decoration.
namespace
{
    inline constexpr std::array<std::byte, 16> kLauncherName = {
        std::byte{ 'P' }, std::byte{ 'l' }, std::byte{ 'a' }, std::byte{ 'y' },
        std::byte{ 'G' }, std::byte{ 'T' }, std::byte{ 'A' }, std::byte{ 'I' },
        std::byte{ 'V' }, std::byte{ '.' }, std::byte{ 'e' }, std::byte{ 'x' },
        std::byte{ 'e' }, Bytes::Zero, std::byte{ 'e' }, Bytes::Zero,
    };

    // "LaunchGTAIV.exe\0", the text both slots currently hold.
    inline constexpr std::string_view kLauncherSlot = "4C 61 75 6E 63 68 47 54 41 49 56 2E 65 78 65 00";

    void InstallLaunchWithPlayGTAIV()
    {
        if (!Loader::Enabled("LaunchWithPlayGTAIV"))
            return;

        // The launch slots exist in the game process, which is where this arm
        // belongs: the offline dump has no module context, and the original only
        // rewrites the slots when PlayGTAIV.exe is the launcher.
        if (Game::Get().Module() == nullptr)
            return;

        if (!BuildIs(Loader::Current(),
                { Build::v1000, Build::v1040, Build::v1060, Build::v1070, Build::v1080 }))
            return;

        auto pattern = Pattern(kLauncherSlot);
        for (size_t index = 0; index < pattern.size(); ++index)
        {
            auto* slot = pattern.get(index).get<std::byte>();
            injector::WriteMemoryRaw(reinterpret_cast<uintptr_t>(slot),
                const_cast<std::byte*>(kLauncherName.data()), kLauncherName.size(), true);
        }
    }
}

// Registered like every other arm: the install tier owns the gate.
export void RegisterLauncher()
{
    Loader::InstallEvent() += [] { InstallLaunchWithPlayGTAIV(); };
}
