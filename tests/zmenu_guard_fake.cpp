#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>

namespace {
std::uint32_t g_test_version = 0;
}

extern "C" __declspec(dllexport) void __cdecl ZMenuVersionTestSet(
    std::uint32_t version) {
  g_test_version = version;
}

extern "C" __declspec(dllexport) int __cdecl GetZMenuVersion(
    std::uint32_t* major, std::uint32_t* minor) {
  if (major != nullptr) *major = g_test_version;
  if (minor != nullptr) *minor = 7u;
  return -1;  // Original sub_10052300 ignores the export's return value.
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
