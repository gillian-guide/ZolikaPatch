#include <common.hxx>

import common;
import config;
import game;
import loader;
import options;

extern "C" __declspec(dllexport) void InitializeASI();

// The startup driver. Like the original, everything happens synchronously on
// DLL_PROCESS_ATTACH: the DRM has already rewritten the game's code by the time
// a deferred thread would run, so the signatures have to be matched now. The arm
// pipeline is the next module in; this driver stops after the identity and the
// configuration it has to read.
namespace
{
    std::once_flag startupFlag;
    std::atomic<bool> started{ false };

    void StartUp(HMODULE module)
    {
        auto& game = Game::Get();
        game.Initialise(module);

#ifdef ZP_TRACE
        Log::Open(game.LogPath());
        Log::Print("{} {}", rsc_ProductName, rsc_ProductVersion);
        Log::Print("module: {}", game.ModulePath().string());
#endif

        const bool read = Config::Load();
        Loader::Current() = Classify(game.FileVersion());
        const auto build = Loader::Current();

#ifdef ZP_TRACE
        Log::Print("game: {} {} (image {:#010x}+{:#x})", game.ExecutablePath().string(),
            game.FileVersionString(), game.ImageBase(), game.ImageSize());
        Log::Print("ini: {} ({})", std::string(Config::FileName), read ? "read" : "absent");
        Log::Print("build: {} ({})", BuildName(build), CompactVersion(game.FileVersion()));
#endif

        FusionFix::onInitEvent().executeAll();
        Loader::Run();

#ifdef ZP_TRACE
        Log::Print("options: {}", Catalogue::Count());
#endif
    }

    void ShutDown()
    {
        if (!started)
            return;
        started = false;
        FusionFix::onShutdownEvent().executeAll();
        Log::Close();
    }
}

extern "C" __declspec(dllexport) void InitializeASI()
{
    std::call_once(startupFlag, []
    {
        started = true;
        StartUp(GetThisModuleHandle());
    });
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(module);
        InitializeASI();
        break;
    case DLL_PROCESS_DETACH:
        ShutDown();
        break;
    }
    return TRUE;
}
