// Windows bodies for the SkipLauncher hook installs. The originals live at
// 0x10004720..0x1000484e (file API forwarders, the exception-guarded
// CloseHandle, the CreateFile slot forwarder and the "return 1" dfaInit) and
// 0x100041a0..0x10004570 (the WM_ROM message-window service, ANSI variant).
// Provenance: written from the decompilation of ZolikaPatch's handlers; the
// feature's upstream design is XLivelessAddon (no license), which was used as
// behavioral reference only. See CREDITS.md.
#include "zpatch/skip_launcher_win.hpp"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>

namespace zpatch {
namespace {

constexpr UINT kRomMessage = 43858;      // WM_ROM in the original
constexpr char kRomClass[] = "MessageWindow";
constexpr char kRomClassFallback[] = "MessageWindow2";

// xmmword_10104050..0x10104080: the table the ROM service hands to the game.
constexpr std::uint32_t kRomTable[16] = {
    8, 11, 241, 1, 240, 2, 7, 12, 3, 243, 9, 6, 10, 5, 4, 242,
};

std::atomic<std::uint32_t> g_create_file_slot{0};
std::atomic<HWND> g_rom_window{nullptr};
std::atomic<bool> g_rom_ready{false};
std::atomic<bool> g_rom_started{false};

using CreateFileAFn = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD,
                                      HANDLE);

LRESULT CALLBACK rom_wndproc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_NCCREATE) {
    g_rom_window.store(window);
    g_rom_ready.store(true);
  } else if (message == WM_DESTROY) {
    g_rom_window.store(nullptr);
    PostQuitMessage(0);
    return 0;
  } else if (message == kRomMessage) {
    auto* payload = reinterpret_cast<std::uint32_t*>(lparam);
    switch (payload[4]) {  // action
      case 18:
        std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(payload[6])), kRomTable,
                    sizeof(kRomTable));
        return 0;
      case 51:
        return static_cast<LRESULT>(payload[5] + payload[6]);
      case 100:
        *reinterpret_cast<std::uint32_t*>(static_cast<std::uintptr_t>(payload[5])) = 1;
        return 0;
      default:
        return 0;
    }
  }
  return DefWindowProcA(window, message, wparam, lparam);
}

DWORD WINAPI rom_thread(LPVOID) {
  WNDCLASSEXA wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = &rom_wndproc;
  wc.hInstance = GetModuleHandleA(nullptr);
  wc.lpszClassName = kRomClass;
  ATOM atom = RegisterClassExA(&wc);
  if (!atom) {
    wc.lpszClassName = kRomClassFallback;
    atom = RegisterClassExA(&wc);
    if (!atom) return 1;
  }
  CreateWindowExA(0, reinterpret_cast<LPCSTR>(static_cast<std::uintptr_t>(atom)), "", 0, 0, 0, 0,
                  0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
  MSG msg;
  while (GetMessageA(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessageA(&msg);
  }
  UnregisterClassA(reinterpret_cast<LPCSTR>(static_cast<std::uintptr_t>(atom)), wc.hInstance);
  return 0;
}

// 0x100043D0: start the ROM service once and wait until the window exists.
void rom_service_start() {
  bool expected = false;
  if (!g_rom_started.compare_exchange_strong(expected, true)) return;
  HANDLE thread = CreateThread(nullptr, 0, &rom_thread, nullptr, 0, nullptr);
  if (thread) CloseHandle(thread);
  for (int i = 0; i < 200 && !g_rom_ready.load(); ++i) Sleep(1);
}

// 0x10004780
int __cdecl skip_dfa_init() { return 1; }

// 0x10004820: forwards through the captured IAT slot when present. The original
// trampoline is __cdecl (plain ret; the caller cleans the seven arguments), so
// the ABI must match exactly - a stdcall body would corrupt the stack.
HANDLE __cdecl skip_create_file(LPCSTR file_name, DWORD desired_access, DWORD share_mode,
                                LPSECURITY_ATTRIBUTES security, DWORD creation_disposition,
                                DWORD flags, HANDLE template_file) {
  const std::uint32_t slot = g_create_file_slot.load();
  if (slot) {
    const auto fn = *reinterpret_cast<CreateFileAFn*>(static_cast<std::uintptr_t>(slot));
    if (fn) return fn(file_name, desired_access, share_mode, security, creation_disposition,
                      flags, template_file);
  }
  return CreateFileA(file_name, desired_access, share_mode, security, creation_disposition, flags,
                     template_file);
}

// 0x10004790: the original wraps CloseHandle in SEH and, on a fault, returns
// `or eax,0xffffffff` (-1, the SEH filter body at 0x100047ec) rather than 1.
// SEH keywords are MSVC-only; the ASI is built with the Visual Studio x86
// toolchain, and other compilers use the plain call. __cdecl like the original
// trampoline.
BOOL __cdecl skip_close_handle(HANDLE handle) {
#if defined(_MSC_VER)
  __try {
    return CloseHandle(handle);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return static_cast<BOOL>(-1);
  }
#else
  return CloseHandle(handle);
#endif
}

// 0x10004760
DWORD __cdecl skip_set_file_pointer(HANDLE handle, LONG distance, PLONG distance_high,
                                    DWORD move_method) {
  return SetFilePointer(handle, distance, distance_high, move_method);
}

// 0x10004720
BOOL __cdecl skip_get_file_size_ex(HANDLE handle, PLARGE_INTEGER size) {
  return GetFileSizeEx(handle, size);
}

// 0x10004740
BOOL __cdecl skip_read_file(HANDLE handle, LPVOID buffer, DWORD to_read, LPDWORD read,
                            LPOVERLAPPED overlapped) {
  return ReadFile(handle, buffer, to_read, read, overlapped);
}

// 0x10004570: start the ROM service, reset the object and publish the window
// handle plus the WM_ROM id the game's patched code sends to. __cdecl with one
// stack argument, returning the window handle in eax (plain ret).
int __cdecl skip_rom_reset(std::uint32_t* object) {
  rom_service_start();
  if (!object) return 0;
  object[0] = 0;
  object[2] = 0;
  object[3] = 0;
  for (std::size_t i = 5; i <= 18; ++i) object[i] = 0;
  object[1] = static_cast<std::uint32_t>(
      reinterpret_cast<std::uintptr_t>(g_rom_window.load()));
  object[4] = kRomMessage;
  return static_cast<int>(object[1]);
}

// 0x10004610: the object arrives in ESI (the patched site keeps it there) and
// the site pushed two arguments the callee must pop - the original is
// `mov ecx,[esi+1c]; mov [esi+20],ecx; mov al,1; ret 8`. A C function cannot
// express that, so it is reproduced instruction-for-instruction.
#if defined(_MSC_VER)
__declspec(naked) void skip_rom_set() {
  __asm {
    mov ecx, [esi + 0x1c]
    mov [esi + 0x20], ecx
    mov al, 1
    ret 8
  }
}
#else
__attribute__((naked)) void skip_rom_set() {
  __asm__ __volatile__(
      "movl 0x1c(%esi), %ecx\n"
      "movl %ecx, 0x20(%esi)\n"
      "movb $1, %al\n"
      "ret $8\n");
}
#endif

}  // namespace

void skip_launcher_win_set_create_file_slot(std::uint32_t slot_va) noexcept {
  g_create_file_slot.store(slot_va);
}

std::uintptr_t skip_launcher_win_resolve(SkipHook hook, void*) noexcept {
  switch (hook) {
    case SkipHook::dfa_init:
      return reinterpret_cast<std::uintptr_t>(&skip_dfa_init);
    case SkipHook::create_file:
      return reinterpret_cast<std::uintptr_t>(&skip_create_file);
    case SkipHook::close_handle:
      return reinterpret_cast<std::uintptr_t>(&skip_close_handle);
    case SkipHook::set_file_pointer:
      return reinterpret_cast<std::uintptr_t>(&skip_set_file_pointer);
    case SkipHook::get_file_size_ex:
      return reinterpret_cast<std::uintptr_t>(&skip_get_file_size_ex);
    case SkipHook::read_file:
      return reinterpret_cast<std::uintptr_t>(&skip_read_file);
    case SkipHook::rom_reset:
      return reinterpret_cast<std::uintptr_t>(&skip_rom_reset);
    case SkipHook::rom_set:
      return reinterpret_cast<std::uintptr_t>(&skip_rom_set);
  }
  return 0;
}

}  // namespace zpatch

#endif  // _WIN32
