module;

#include <common.hxx>

export module graphics;

import bodies;
import bytes;
import common;
import loader;
import pattern;

// BuildingDynamicShadows' second site jumps to the shim at recovered VA
// 0x10005590 (`sub_10005590`): a __thiscall that stores the caller's two dwords
// into the object in ECX, derives a flag from the second one, and returns with
// `ret 12`. It reads nothing of ours, so its instruction stream carries no module
// operand and can be proved instruction-for-instruction.
static __declspec(naked) void BuildingsDynamicShadowsShim()
{
    __asm
    {
        cmp ecx, 0
        je done
        mov eax, DWORD PTR [esp + 8]
        mov DWORD PTR [ecx + 0x20], eax
        mov eax, DWORD PTR [esp + 0xc]
        mov DWORD PTR [ecx + 0x24], eax
        neg eax
        sbb eax, eax
        and eax, DWORD PTR [esp + 4]
        mov DWORD PTR [ecx + 0x1c], eax
    done:
        ret 12
    }
}

namespace
{
    // BuildingDynamicShadows: the shadow-size operand at the call site becomes zero
    // (0x104 -> 0x100) and the second site jumps into the shim.
    void InstallBuildingDynamicShadows()
    {
        if (!Loader::Enabled("BuildingDynamicShadows"))
            return;

        auto operand = Pattern("50 68 04 01 00 00 6A 02 6A 00 E8");
        if (operand.size() > 0)
            injector::WriteMemory<std::byte>(MatchAddress(operand, 2), Bytes::Zero, true);

        auto site = Pattern("8B 47 28 C1 E8 06 83 E0 0F 83 C0");
        if (site.size() > 0)
        {
            Bodies::Register(reinterpret_cast<uintptr_t>(&BuildingsDynamicShadowsShim), 0x10005590u);
            injector::MakeJMP(MatchAddress(site, 0),
                reinterpret_cast<void*>(&BuildingsDynamicShadowsShim), true);
        }
    }
}

// Registered like every other arm: the install tier owns the gate.
export void RegisterGraphics()
{
    Loader::InstallEvent() += [] { InstallBuildingDynamicShadows(); };
}
