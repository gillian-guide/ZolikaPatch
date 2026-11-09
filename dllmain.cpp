#include "pch.h"
#define WIN32_LEAN_AND_MEAN 
#include <windows.h>
#include <stdint.h>
#include <map>
#include <d3d9.h>
#include <shlwapi.h>
//#include <d3dx9.h>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include "injector\injector.hpp"
#include "injector\assembly.hpp"
#include "injector\calling.hpp"
#include "injector\hooking.hpp"
#include "injector\utility.hpp"
#include "Hooking.Patterns.h"
#include "Hooking.cpp"
#include "INI.h"
#include "SHLWAPI.h"
#include "xlive\xliveless.cpp"

DWORD dwLoadOffset;

//#define CEMP // never again
#ifdef CEMP
#include "g:\SteamLibrary\steamapps\common\Grand Theft Auto IV\GTAIV\MinHook.h"
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "g:\\SteamLibrary\\steamapps\\common\\Grand Theft Auto IV\\GTAIV\\MinHook.x86.lib")
#endif

LRESULT WINAPI DefWindowProcWProxy(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	static bool bOnce = false;
	if (!bOnce)
	{
		SetWindowLong(hWnd, GWL_STYLE, GetWindowLong(hWnd, GWL_STYLE) & ~WS_OVERLAPPEDWINDOW);
		bOnce = true;
	}

	return DefWindowProcW(hWnd, Msg, wParam, lParam);
}

LRESULT WINAPI DefWindowProcAProxy(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	static bool bOnce = false;
	if (!bOnce)
	{
		SetWindowLong(hWnd, GWL_STYLE, GetWindowLong(hWnd, GWL_STYLE) & ~WS_OVERLAPPEDWINDOW);
		bOnce = true;
	}

	return DefWindowProcA(hWnd, Msg, wParam, lParam);
}

injector::hook_back<void(__cdecl*)(int)> hbsub_7870A0;
void __cdecl sub_7870A0(int a1)
{
	static bool bOnce = false;
	if (!bOnce)
	{
		if (a1 == 0)
		{
			bool bNoLoad = (GetAsyncKeyState(VK_SHIFT) & 0xF000) != 0;
			if (!bNoLoad)
				a1 = 6;

			bOnce = true;
		}
	}
	return hbsub_7870A0.fun(a1);
}

injector::hook_back<int32_t(*)()> hbGetVidMem;
int32_t GetVidMem()
{
	auto v = hbGetVidMem.fun();
	return v <= 0 ? INT_MAX : v;
}

DWORD base;

int vehicleStructsLimit = 1024;
const char* strVehicleStruct = "VehicleStruct";
uintptr_t patch_vehicleStructs_push_goBack = 0;


#define ASM_ACCESS_LIMIT_VAR(limitObject, limitType, limitMember, type) type ptr[offset g_LimitAdjuster + CLimitAdjuster::limitObject + limitType::limitMember]
#define ASM_ACCESS_LIMIT_VAR_8_BIT(limitObject, limitType, limitMember) ASM_ACCESS_LIMIT_VAR(limitObject, limitType, limitMember, byte)
#define ASM_ACCESS_LIMIT_VAR_16_BIT(limitObject, limitType, limitMember) ASM_ACCESS_LIMIT_VAR(limitObject, limitType, limitMember, word)
#define ASM_ACCESS_LIMIT_VAR_32_BIT(limitObject, limitType, limitMember) ASM_ACCESS_LIMIT_VAR(limitObject, limitType, limitMember, dword)
#define ASM_ACCESS_LIMIT_VAR_64_BIT(limitObject, limitType, limitMember) ASM_ACCESS_LIMIT_VAR(limitObject, limitType, limitMember, qword)

//void patch_vehicleStructs_push()
static void __declspec(naked) patch_vehicleStructs_push()
{
	__asm
	{
		push strVehicleStruct;	// push "VehicleStruct"
		push dword ptr[vehicleStructsLimit];
		//push ASM_ACCESS_LIMIT_VAR_32_BIT(m_dynamicLimits, DynamicLimits, vehicleStructsLimit);

		// go back
		jmp[patch_vehicleStructs_push_goBack];
	}
}

struct CEpisode
{
	char path[260];
	char pad[64];
	char device[20]; // +324
	int deviceType; // +344
	char pad2[8];
	char pad3[2];
	char useThis; // +358; checked before calling the 'scan and process episode setup' func
	char unknown1; // +359
};

#define EAXJMP(a) { _asm mov eax, a _asm jmp eax }

struct CEpisodes
{
	char pad[332];
	CEpisode* episodes; // +332
	short numEpisodes; // +336
	short numAllocatedEpisodes; // +338
	char pad2[18]; // +340
	char unknownScanFlag; // +358

	void addEpisode(const CEpisode* episode) { EAXJMP(0x813E20); }
};

auto ProcessEpisode = (void(__stdcall*)(int episodeMask, CEpisode* episode, int unknown1))0x813480;

void AddEpisode(CEpisodes* episodes, int num, const char* path)
{
	CEpisode ep;
	memset(&ep, 0, sizeof(ep));
	strcpy(ep.path, path);
	ep.useThis = 1;
	ep.deviceType = 1;

	ProcessEpisode(num, &ep, 1);

	episodes->addEpisode(&ep);
}

void __fastcall ScanGameEpisodes(CEpisodes* episodes)
{
	//std::wstring citizenPath = MakeRelativeCitPath(L"citizen");
	//char citizenPathStr[256];
	//
	//wcstombs(citizenPathStr, citizenPath.c_str(), sizeof(citizenPathStr));
	//
	//AddEpisode(episodes, 4, citizenPathStr);

	HKEY regKey;
	if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, "SOFTWARE\\Rockstar Games\\EFLC", 0, KEY_READ, &regKey) == ERROR_SUCCESS)
	{
		char installFolder[512];
		DWORD numBytes = sizeof(installFolder);

		if (RegQueryValueExA(regKey, "InstallFolder", 0, nullptr, (LPBYTE)installFolder, &numBytes) == ERROR_SUCCESS)
		{
			if (GetFileAttributesA(installFolder) != INVALID_FILE_ATTRIBUTES)
			{
				char pathStr[256];
				strcpy(pathStr, installFolder);
				strcat(pathStr, "\\TLAD\\");

				AddEpisode(episodes, 2, pathStr);

				strcpy(pathStr, installFolder);
				strcat(pathStr, "\\TBoGT\\");

				AddEpisode(episodes, 8, pathStr);
			}
		}
	}

	episodes->unknownScanFlag = 2;
}

typedef struct sInt
{
	int value;
};
double mousefixhook4(void*, int axis)
{
	if (axis == 0) {
		return *(int32_t*)(base + 0x1224DC4) * 0.0078125;
	}
	else if (axis == 1) {
		return *(int32_t*)(base + 0x1224DB4) * 0.0078125;
	}
	return 0.0;
}
int ADDRESS_MOUSE_X;
int ADDRESS_MOUSE_Y;
double mousefixhookpattern(void*, int axis)
{
	if (axis == 0) return *(int*)(ADDRESS_MOUSE_X) * 0.0078125;
	else if (axis == 1) return *(int*)(ADDRESS_MOUSE_Y) * 0.0078125;
	return 0.0;
}
/*void SuperLODFix(__m128i* a1, const __m128i* a2, unsigned int a3)
{
	const __m128i* v3; // esi
	__m128i* v4; // edi
	unsigned int v5; // ecx
	__m128i v6; // xmm1
	__m128i v7; // xmm2
	__m128i v8; // xmm3
	__m128i v9; // xmm5
	__m128i v10; // xmm6
	__m128i v11; // xmm7

	if (a2)
	{
		v3 = a2;
		v4 = a1;
		v5 = a3 >> 7;
		do
		{
			v6 = _mm_load_si128(v3 + 1);
			v7 = _mm_load_si128(v3 + 2);
			v8 = _mm_load_si128(v3 + 3);
			_mm_store_si128(v4, _mm_load_si128(v3));
			_mm_store_si128(v4 + 1, v6);
			_mm_store_si128(v4 + 2, v7);
			_mm_store_si128(v4 + 3, v8);
			v9 = _mm_load_si128(v3 + 5);
			v10 = _mm_load_si128(v3 + 6);
			v11 = _mm_load_si128(v3 + 7);
			_mm_store_si128(v4 + 4, _mm_load_si128(v3 + 4));
			_mm_store_si128(v4 + 5, v9);
			_mm_store_si128(v4 + 6, v10);
			_mm_store_si128(v4 + 7, v11);
			v3 += 8;
			v4 += 8;
			--v5;
		} while (v5);
	}
}*/
inline void* qmemcpy(void* dst, const void* src, size_t cnt)
{
	char* out = (char*)dst;
	const char* in = (const char*)src;
	while (cnt > 0)
	{
		*out++ = *in++;
		--cnt;
	}
	return dst;
}
/*__m128i* __cdecl sub_91C32D(__m128i* a1, const __m128i* a2, unsigned int a3)
{
	int v3; // ecx
	__m128i* result; // eax
	int v5; // ecx
	int v6; // ecx
	unsigned int v7; // [esp+4h] [ebp-18h]

	v3 = (signed int)a2 % -16;
	result = a1;
	if ((signed int)a1 % -16 | (signed int)a2 % -16)
	{
		if (v3 == (signed int)a1 % -16)
		{
			v6 = -v3;
			qmemcpy(a1, a2, v6 + 16);
			sub_91C32D((__m128i*)&a1[1].m128i_i8[v6], (__m128i*)&a2[1].m128i_i8[v6], a3 - (v6 + 16));
		}
		else
		{
			qmemcpy(a1, a2, a3);
		}
		result = a1;
	}
	else
	{
		v5 = a3 & 0x7F;
		v7 = v5;
		if (a3 != v5)
		{
			SuperLODFix(a1, a2, a3 - v5);
			result = a1;
			v5 = v7;
		}
		if (v5)
		{
			if ((char*)a2->m128i_i32 && (char*)result->m128i_i32)
			{
				qmemcpy((char*)result->m128i_i32 + a3 - v5, (char*)a2->m128i_i32 + a3 - v5, v7);
				result = a1;
			}
			result = a1;
		}
	}
	return result;
}*/
typedef int _DWORD;
typedef short _WORD;
void sub_239760(unsigned __int16* a1, int a2, int a3)
{
	unsigned __int16* v3; // edi
	int v4; // ebx
	int i; // esi
	int v6; // ecx
	int v7; // eax

	v3 = a1;
	v4 = a1[6];
	for (i = 0; i < v4; ++i)
	{
		v6 = *(_DWORD*)(*(_DWORD*)(*((_DWORD*)v3 + 14) + 8 * a2 - 8) + 4 * i);
		if (v6)
		{
			v7 = *(_DWORD*)(*((_DWORD*)v3 + 2) + 4 * i);
			((void(__cdecl*)(void*, _DWORD*, int, int))(base + 0x229A40))(*(void**)(v7 + 24), (_DWORD*)(v7 + 20), v6, a3);
		}
	}
}
HANDLE CreateFileHook(_In_ LPCSTR lpFileName, _In_ DWORD dwDesiredAccess, _In_ DWORD dwShareMode, _In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes, _In_ DWORD dwCreationDisposition, _In_ DWORD dwFlagsAndAttributes, _In_opt_ HANDLE hTemplateFile)
{
	return CreateFileA(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
}

BOOL CloseHandleHook(_In_ HANDLE hObject)
{
	__try
	{
		return CloseHandle(hObject);
	}
	__except (EXCEPTION_CONTINUE_EXECUTION)
	{
	}

	return TRUE;
}

BOOL GetFileSizeExHook(HANDLE hFile, PLARGE_INTEGER lpFileSize)
{
	return GetFileSizeEx(hFile, lpFileSize);
}

BOOL ReadFileHook(HANDLE hFile, LPVOID pBuffer, DWORD nNumberOfBytesRead, LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped)
{
	return ReadFile(hFile, pBuffer, nNumberOfBytesRead, lpNumberOfBytesRead, lpOverlapped);
}

DWORD SetFilePointerHook(HANDLE hFile, LONG dtm, PLONG dtmHigh, DWORD mm)
{
	return SetFilePointer(hFile, dtm, dtmHigh, mm);
}

void __stdcall SendMessageFakie(int, int, int, int)
{

}

int __cdecl dfaInit(int, int, int)
{
	return 1;
}

char RGSCLoginHook()
{
	return 1;
}

//char launchivoverride[] = "PlayGTAIV.exe";

bool fixmoddedfilespatch4;
bool fixgfwlupdate;
unsigned long long baseAddressxlive;

unsigned int getnewver()
{
	return 0x10000E83;
}

bool mousesens;

// -6 is the lowest it can go, CFC2E0 on patch 8
int MOUSE_SENSITIVITY_OVERRIDE;
int mousesensaddr;

DWORD WINAPI SetStuff(HMODULE hModule)
{
	while (1)
	{
		if (fixmoddedfilespatch4)
		{
			auto pattern = hook::pattern("33 C4 89 84 24 A8 01 00 00 53 8B 5D 08 56 57");
			if (pattern.size() > 0)
			{
				int addr = *(int*)(pattern.get(0).get<uintptr_t>(0x1E));
				injector::WriteMemory(addr, 0);
			}
		}
		if (fixgfwlupdate)
		{

			//injector::MakeCALL(baseAddressxlive + 0x4FCF7C, getnewver);
			injector::WriteMemory(baseAddressxlive + 0x66F770, 0x10000E83, 1);
			injector::WriteMemory(baseAddressxlive + 0x66F774, 0x10000E83, 1);
			injector::WriteMemory(baseAddressxlive + 0x6B55D0, 0x10000E83, 1); // this is the one we need
			injector::WriteMemory(baseAddressxlive + 0x6B55F0, 0x10000E83, 1);
			//injector::WriteMemory<short>(baseAddressxlive + 0x4FCF92, 0x9090, 1);
		}
		if (mousesens)
		{
			MOUSE_SENSITIVITY_OVERRIDE = *(int*)(mousesensaddr) - 6;
		}
		Sleep(1000);
	}
	FreeLibraryAndExitThread(hModule, 0);
	return 0;
}

int pedComp(const char* a1, const char* a2)
{
	if ((a2[4] == 'h') || (a2[4] == 'p' && a2[5] == 'e')) return 0;
	return 2;
}

int vehComp(const char* a1, const char* a2)
{
	if (a2[4] == 'v') return 0;
	return 2;
}

void __declspec(naked) GTAPedComparisonASM()
{
	__asm
	{
		push    ebx
		push    esi
		push    edi
		mov     edi, ecx
		xor esi, esi
		cmp[edi + 0Ch], si
		jbe     short loc_399FB
		mov     ebx, [esp + 10h]

		loc_399D1:
		mov     eax, [edi + 8]
			mov     ecx, [eax + esi * 4]
			test    ecx, ecx
			jz      short loc_399F0
			mov     edx, [ecx]
			mov     eax, [edx + 10h]
			call    eax
			push    eax
			push    ebx
			call    pedComp
			add     esp, 8
			test    eax, eax
			jz      short loc_39A03

			loc_399F0 :
		movzx   ecx, word ptr[edi + 0Ch]
			add     esi, 1
			cmp     esi, ecx
			jl      short loc_399D1

			loc_399FB :
		pop     edi
			pop     esi
			xor eax, eax
			pop     ebx
			retn    4

			loc_39A03 :
			mov     edx, [edi + 8]
			mov     eax, [edx + esi * 4]
			pop     edi
			pop     esi
			pop     ebx
			retn    4
	}
}

void __declspec(naked) GTAVehicleComparisonASM()
{
	__asm
	{
		push    ebx
		push    esi
		push    edi
		mov     edi, ecx
		xor esi, esi
		cmp[edi + 0Ch], si
		jbe     short loc_399FB
		mov     ebx, [esp + 10h]

		loc_399D1:
		mov     eax, [edi + 8]
			mov     ecx, [eax + esi * 4]
			test    ecx, ecx
			jz      short loc_399F0
			mov     edx, [ecx]
			mov     eax, [edx + 10h]
			call    eax
			push    eax
			push    ebx
			call    vehComp
			add     esp, 8
			test    eax, eax
			jz      short loc_39A03

			loc_399F0 :
		movzx   ecx, word ptr[edi + 0Ch]
			add     esi, 1
			cmp     esi, ecx
			jl      short loc_399D1

			loc_399FB :
		pop     edi
			pop     esi
			xor eax, eax
			pop     ebx
			retn    4

			loc_39A03 :
			mov     edx, [edi + 8]
			mov     eax, [edx + esi * 4]
			pop     edi
			pop     esi
			pop     ebx
			retn    4
	}
}

bool VSYNC_FIX = 0;

char return1hook()
{
	return 1;
}

#ifdef CEMP

_DWORD* __cdecl IS_MULTIPLAYER_ENABLED(_DWORD* a1)
{
	int v1; // ecx
	_DWORD* result; // eax

	v1 = 1;
	result = (_DWORD*)*a1;
	*(_DWORD*)*a1 = v1;
	return result;
}

template<typename T>
inline T* getRVA(uintptr_t rva)
{
#ifdef _M_IX86
	return (T*)(dwLoadOffset + 0x400000 + rva);
#elif defined(_M_AMD64)
	return (T*)(baseAddressDifference + 0x140000000 + rva);
#endif
}

template<typename TOrdinal>
inline bool iat_matches_ordinal(uintptr_t* nameTableEntry, TOrdinal ordinal)
{

}

template<>
inline bool iat_matches_ordinal(uintptr_t* nameTableEntry, int ordinal)
{
	if (IMAGE_SNAP_BY_ORDINAL(*nameTableEntry))
	{
		return IMAGE_ORDINAL(*nameTableEntry) == ordinal;
	}

	return false;
}

template<>
inline bool iat_matches_ordinal(uintptr_t* nameTableEntry, const char* ordinal)
{
	if (!IMAGE_SNAP_BY_ORDINAL(*nameTableEntry))
	{
		auto import = getRVA<IMAGE_IMPORT_BY_NAME>(*nameTableEntry);

		return !_stricmp(import->Name, ordinal);
	}

	return false;
}

template<typename T, typename TOrdinal>
T iat(const char* moduleName, T function, TOrdinal ordinal)
{
#ifdef _M_IX86
	IMAGE_DOS_HEADER* imageHeader = (IMAGE_DOS_HEADER*)(dwLoadOffset + 0x400000);
#elif defined(_M_AMD64)
	IMAGE_DOS_HEADER* imageHeader = (IMAGE_DOS_HEADER*)(baseAddressDifference + 0x140000000);
#endif
	IMAGE_NT_HEADERS* ntHeader = getRVA<IMAGE_NT_HEADERS>(imageHeader->e_lfanew);

	IMAGE_IMPORT_DESCRIPTOR* descriptor = getRVA<IMAGE_IMPORT_DESCRIPTOR>(ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

	while (descriptor->Name)
	{
		const char* name = getRVA<char>(descriptor->Name);

		if (_stricmp(name, moduleName))
		{
			descriptor++;

			continue;
		}

		if (descriptor->OriginalFirstThunk == 0)
		{
			return nullptr;
		}

		auto nameTableEntry = getRVA<uintptr_t>(descriptor->OriginalFirstThunk);
		auto addressTableEntry = getRVA<uintptr_t>(descriptor->FirstThunk);

		while (*nameTableEntry)
		{
			if (iat_matches_ordinal(nameTableEntry, ordinal))
			{
				T origEntry = (T)*addressTableEntry;

				DWORD oldProtect;
				VirtualProtect(addressTableEntry, sizeof(T), PAGE_READWRITE, &oldProtect);

				*addressTableEntry = (uintptr_t)function;

				VirtualProtect(addressTableEntry, sizeof(T), oldProtect, &oldProtect);

				return origEntry;
			}

			nameTableEntry++;
			addressTableEntry++;
		}

		return nullptr;
	}

	return nullptr;
}

//template<typename T>
int get_call(int address)
{
	intptr_t target = *(uintptr_t*)(address + 1);
	target += (address + 5);

	return target;
}


#include <WS2tcpip.h>

template<bool IsBoolean>
struct fwEventConnectProxy
{
	template<typename... Args>
	struct Internal
	{
		template<typename TEvent, typename TFunc>
		static void Proxy(TEvent& event, TFunc func, int order)
		{
			event.ConnectInternal(func, order);
		}
	};
};

template<>
struct fwEventConnectProxy<false>
{
	template<typename... Args>
	struct Internal
	{
		template<typename TEvent, typename TFunc>
		static void Proxy(TEvent& event, TFunc func, int order)
		{
			event.ConnectInternal([=](Args... args)
			{
				func(args...);
				return true;
			}, order);
		}
	};
};

template<typename... Args>
class fwEvent
{
public:
	friend struct fwEventConnectProxy<true>;
	friend struct fwEventConnectProxy<false>;

	typedef std::function<bool(Args...)> TFunc;

private:
	struct callback
	{
		TFunc function;
		callback* next;
		int order;

		callback(TFunc func)
			: function(func)
		{

		}
	};

	callback* m_callbacks;

public:
	fwEvent()
	{
		m_callbacks = nullptr;
	}

	~fwEvent()
	{
		Reset();
	}

	template<typename T>
	void Connect(T func)
	{
		fwEventConnectProxy<std::is_same<typename std::result_of<decltype(&T::operator())(T, Args...)>::type, bool>::value>::template Internal<Args...>::Proxy(*this, func, 0);
	}

	template<typename T>
	void Connect(T func, int order)
	{
		fwEventConnectProxy<std::is_same<typename std::result_of<decltype(&T::operator())(T, Args...)>::type, bool>::value>::template Internal<Args...>::Proxy(*this, func, order);
	}

	void Reset()
	{
		callback* cb = m_callbacks;

		while (cb)
		{
			callback* curCB = cb;

			cb = cb->next;

			delete curCB;
		}

		m_callbacks = nullptr;
	}

private:
	void ConnectInternal(TFunc func, int order)
	{
		auto cb = new callback(func);
		cb->order = order;

		if (!m_callbacks)
		{
			cb->next = nullptr;
			m_callbacks = cb;
		}
		else
		{
			callback* cur = m_callbacks;
			callback* last = nullptr;

			while (cur && order >= cur->order)
			{
				last = cur;
				cur = cur->next;
			}

			cb->next = cur;

			(!last ? m_callbacks : last->next) = cb;
		}
	}

	void ConnectInternal(TFunc func)
	{
		ConnectInternal(func, 0);
	}

public:
	bool operator()(Args... args)
	{
		if (!m_callbacks)
		{
			return true;
		}

		for (callback* cb = m_callbacks; cb; cb = cb->next)
		{
			if (cb->function && !cb->function(args...))
			{
				return false;
			}
		}

		return true;
	}
};

__declspec(dllexport) fwEvent<> OnGameFrame;

int __stdcall CfxSendTo(SOCKET s, char* buf, int len, int flags, sockaddr* to, int tolen)
{
	sockaddr_in* toIn = (sockaddr_in*)to;

	char publicAddr[256];
	inet_ntop(AF_INET, &toIn->sin_addr.s_addr, publicAddr, sizeof(publicAddr));

	if (toIn->sin_addr.s_addr == 0xFFFFFFFF)
	{
		return len;
	}

	//trace("CfxSendTo (to %s:%i) %i b\n", publicAddr, htons(toIn->sin_port), len);

	return sendto(s, buf, len, flags, to, tolen);
}

int __stdcall CfxBind(SOCKET s, sockaddr* addr, int addrlen)
{
	sockaddr_in* addrIn = (sockaddr_in*)addr;

	//trace("binder on %i is %p\n", htons(addrIn->sin_port), (void*)s);

	if (htons(addrIn->sin_port) == 6672)
	{
		//if (wcsstr(GetCommandLine(), L"cl2"))
		//{
		//	addrIn->sin_port = htons(6673);
		//}
	}

	return bind(s, addr, addrlen);
}

namespace rage
{
	struct netSocket
	{
		size_t a;
		SOCKET sock;
	};

	struct netRelayEventPacketReceived
	{
		void* fakeVtbl;
		uint8_t pad[28];
		int type; // 2;
		char addressBuf[16];
		void* data;
		size_t len;
	};

	struct netPeerAddress
	{
		uint64_t thing1;
		uint64_t thing2;
		uint32_t thing3;
		uint32_t lanIP;
		uint16_t lanPort;
		uint32_t onlineIP;
		uint16_t onlinePort;
		uint32_t unkIP;
		uint16_t unkPort;
		// we don't care about anything after currently
	};
}

void(*g_origSocketInit)();
static rage::netSocket* socketPtr;

bool(__cdecl* g_origGetLocalPeerAddress)(rage::netPeerAddress* out);

int(_fastcall* g_origParsePacket)(int*, void*);

int __fastcall ParsePacketHook(int* a1, void* a2)
{
	auto result = g_origParsePacket(a1, a2);
	//trace(__FUNCTION__ " packet: %i\n", *a1);
	return result;
}

bool __cdecl GetLocalPeerAddressHook(rage::netPeerAddress* out)
{
	auto success = g_origGetLocalPeerAddress(out);

	if (getenv("ROS_TEMP_LAN_IP"))
	{
		auto a = htonl(inet_addr(getenv("ROS_TEMP_LAN_IP")));;
		auto b = 6672;

		//if (wcsstr(GetCommandLine(), L"cl2"))
		//{
		//	a = htonl(inet_addr("127.0.0.1"));
		//	b = 6673;
		//}

		out->lanIP = a;
		out->onlineIP = a;
		out->unkIP = a;
		out->lanPort = b;
		out->onlinePort = b;
		out->unkPort = b;
	}

	//out->unkIP = GetTickCount(); // kinda shitty but irrelevant later

	return success;
}

template<typename ValueType, typename AddressType>
inline void putVP(AddressType address, ValueType value)
{
	//adjust_base(address);

	DWORD oldProtect;
	VirtualProtect((void*)address, sizeof(value), PAGE_EXECUTE_READWRITE, &oldProtect);

	memcpy((void*)address, &value, sizeof(value));

	VirtualProtect((void*)address, sizeof(value), oldProtect, &oldProtect);
}

void SocketInitHook()
{
	socketPtr = *hook::get_pattern<rage::netSocket*>("8D 44 24 0C 50 56 FF 75 0C", 10);
	((void(__fastcall*)(void*, uint16_t))hook::get_pattern("F6 86 A0 00 00 00 20 75 3A", -0x29))(socketPtr, 6672); // bind wrap

	struct OnlineAddress
	{
		uint32_t ip1;
		uint16_t port1;
		uint32_t ip2;
		uint16_t port2;
	};

	// yes, the relay is active
	putVP<BYTE>(hook::get_pattern("80 3D ? ? ? ? 00 74 32 6A 00 68", 2), 1);

	auto onlineAddress = *hook::get_pattern<OnlineAddress*>("50 53 8D 44 24 24 50 68 ? ? ? ? 8D", 8);
	static auto onRelayReceived = ((void(__thiscall*)(void*, void*))hook::get_pattern("8B 44 24 04 83 78 20 02"));
	static auto receiveWrap = ((int(__thiscall*)(rage::netSocket*, void* address, void* buf, size_t len, int* outErr))hook::get_pattern("83 EC 1C 53 55 8B E9 56 83 7D 04"));

	if (getenv("ROS_TEMP_LAN_IP"))
	{
		auto a = htonl(inet_addr(getenv("ROS_TEMP_LAN_IP")));;
		auto b = 6672;

		//if (wcsstr(GetCommandLine(), L"cl2"))
		//{
		//	a = htonl(inet_addr("127.0.0.1"));
		//	b = 6673;
		//}

		onlineAddress->ip1 = onlineAddress->ip2 = a;
		onlineAddress->port1 = onlineAddress->port2 = b;
	}

	OnGameFrame.Connect([]()
	{
		static auto connectionMgrPtr = *hook::get_pattern<void*>("8b 01 6A 07 68", 5);

		uint8_t from[8]; // actually: netSocketAddress
		uint8_t buf[1400];

		int rlen = receiveWrap(
			socketPtr,
			&from,
			buf,
			sizeof(buf),
			NULL
		); // receive wrap

		if (rlen > 0)
		{
			//trace(__FUNCTION__ " got connection %d\n", rlen);

			rage::netRelayEventPacketReceived event;
			event.type = 2;
			//memcpy(event.addressBuf, from, 8);
			memset(event.addressBuf, 0, 16);
			*(uint32_t*)&event.addressBuf[0] = 0xFFFFFFFF; // relay
			memcpy(&event.addressBuf[8], from, 8); // direct
			event.data = buf;
			event.len = rlen;

			onRelayReceived(connectionMgrPtr, &event);
		}
	});

	g_origSocketInit();
}

void runFrame()
{
	OnGameFrame();
}

#endif

class CVector3
{
public:
	float fX;
	float fY;
	float fZ;
};

struct CPhysical
{
	char pad0[0x10];													// 000-010
	CVector3 m_vPosition;												// 010-01C
	float m_fHeading;													// 01C-020
	char pad1[0xE];														// 020-02E
	short m_nModelIndex;												// 02E-030
	char pad2[0x33];													// 030-063
	char m_nAlpha;														// 063-064
	char pad3[0x8];														// 064-06C
	int* m_pNetworkObject;												// 06C-070
	char pad4[0x8];														// 070-078
	int* m_pAnim;														// 078-07C
	char pad5[0x9C];													// 07C-118
	int m_nProofs;														// 118-11C
	char pad6[0xC8];													// 11C-120
	int* m_pLastDamageEntity;											// 1E4-1E8
	char pad7[0x4];														// 1E8-1EC
	int m_nLastDamageWeapon;											// 1EC-1F0
	float m_fHealth;													// 1F0-1F4
	char pad8[0x1C];													// 1F4-210
};

unsigned int call_0x58AAE0 = 0x58AAE0;

static bool HookIsPedSuperLOD(CPhysical* pPed)
{
	int32_t PlayerModel;
	((uint32_t(__cdecl*)(uint32_t, int32_t&))call_0x58AAE0)(0xAE4B15D6, PlayerModel); // model_superlod

	if (pPed->m_nModelIndex == PlayerModel)
		return true;

	return false;
}

unsigned int jz_0x4A3D37 = 0x4A3D37;
unsigned int jmp_0x4A3CEF = 0x4A3CEF;

void __declspec(naked) SuperLODFix()
{
	__asm
	{
		jmp ActualFunc
JzThing:
		jmp jz_0x4A3D37
ActualFunc:
		push eax
		push ecx
		push esi
		call HookIsPedSuperLOD
		add esp, 4
		pop ecx
		test al, al
		pop eax
		jne SkipLabel
		test byte ptr[eax+72h], 10h
		jz JzThing
SkipLabel:
		jmp jmp_0x4A3CEF
	}
}

unsigned int jmp_5B37B5;
char orig_105879 = 0x7C;
char orig_10587A = 0x22;
unsigned int loc_105879;
unsigned int loc_10587A;
char *ptr_105879 = &orig_105879;
char *ptr_10587A = &orig_10587A;
int oldeax;
int oldebx;
int oldecx;
int oldedx;
int oldesi;
int oldedi;
int oldebp;
int oldesp;
bool ourthingy;
void IsThingyOurThingy(int a1, int a2)
{
	ourthingy = 0;
	//if ((a1 + a2) == (int)loc_105879) ourthingy = 1;
}
void IsThingyOurThingyA(int a1, int a2)
{
	ourthingy = 0;
	//if ((a1 + a2) == (int)loc_10587A) ourthingy = 1;
}
void __declspec(naked) XLiveHorseShit()
{
	__asm
	{
		mov eax, [ebp-8]
		mov edx, esi

		mov[oldeax], eax
		mov[oldebx], ebx
		mov[oldecx], ecx
		mov[oldedx], edx
		mov[oldesi], esi
		mov[oldedi], edi
		mov[oldebp], ebp
		mov[oldesp], esp
		push eax
		push edi
		call IsThingyOurThingy
		cmp ourthingy, 1
		jz IfZero

		mov[oldeax], eax
		mov[oldebx], ebx
		mov[oldecx], ecx
		mov[oldedx], edx
		mov[oldesi], esi
		mov[oldedi], edi
		mov[oldebp], ebp
		mov[oldesp], esp
		push eax
		push edi
		call IsThingyOurThingyA
		cmp ourthingy, 1
		jz IfZeroA

		mov eax, oldeax
		mov ebx, oldebx
		mov ecx, oldecx
		mov edx, oldedx
		mov esi, oldesi
		mov edi, oldedi
		mov ebp, oldebp
		mov esp, oldesp

		movzx eax, byte ptr[eax + edi]
		//mov[oldedx], edx
		//mov[oldesi], esi
		//mov edx, loc_105879
		//cmp eax+edi, edx
		//mov edx, oldedx
		//jz IfZero
		//
		//mov[oldedx], edx
		//mov[oldesi], esi
		//mov edx, loc_10587A
		//cmp eax+edi, edx
		//mov edx, oldedx
		//jz IfZeroA

		jmp jmp_5B37B5
IfZero:
		mov eax, oldeax
		mov ebx, oldebx
		mov ecx, oldecx
		mov edx, oldedx
		mov esi, oldesi
		mov edi, oldedi
		mov ebp, oldebp
		mov esp, oldesp
		movzx eax, byte ptr[ptr_105879]
		jmp jmp_5B37B5
IfZeroA:
		mov eax, oldeax
		mov ebx, oldebx
		mov ecx, oldecx
		mov edx, oldedx
		mov esi, oldesi
		mov edi, oldedi
		mov ebp, oldebp
		mov esp, oldesp
		movzx eax, byte ptr[ptr_10587A]
		jmp jmp_5B37B5
	}
}
unsigned int jmp_540D6C;
void __declspec(naked) PlayerPedFixAgain()
{
	__asm
	{
		mov ecx, [esi+0EA8h]
		cmp ecx, 0CDCDCDCDh
		jz IfZero
		mov byte ptr [ecx+5Ch], 0
IfZero:
		jmp jmp_540D6C
	}
}
unsigned int jmp_463341;
unsigned int jmp_463346;
void __declspec(naked) PlayerPedFixAgain2()
{
	__asm
	{
		cmp eax, 0CDCDCDCDh
		jz IfZero
		mov edx, [eax+58h]
		add esp, 20h
		push edx
		mov ecx, edi
		jmp jmp_463341
		IfZero:
		jmp jmp_463346
	}
}
void FixGFWLPCID()
{
	// my pcid was 0x46364d63d935127c
	// next pcid was 0xfc01554f626dd378
	//int pcid1 = rand();
	//int pcid2 = rand();
	//unsigned long long pcid = pcid1 + pcid2;
	// DG22T-TJWVB-GQJF6-QRBRQ-32K6W eflc key Computer\HKEY_CURRENT_USER\SOFTWARE\Rockstar Games\EFLC\CDKey reg sz
	// GVFG9-FDXJH-JMFWW-HRXQR-83CBG iv key
	SHDeleteValueW(HKEY_CURRENT_USER, L"Software\\Classes\\Software\\Microsoft\\XLive", L"PCID");
	//SHSetValueW(HKEY_CURRENT_USER, L"Software\\Classes\\Software\\Microsoft\\XLive", L"PCID", REG_QWORD, &pcid, 8u);

	//jmp_5B37B5 = baseAddressxlive + 0x5B37B5;
	//loc_105879 = baseAddressxlive + 0x105879;
	//loc_10587A = baseAddressxlive + 0x10587A;
	//injector::MakeJMP(baseAddressxlive + 0x5B37AC, XLiveHorseShit);

	//injector::WriteMemory<WORD>((DWORD)GetModuleHandle("xlive.dll") + 0x105879, 0x9090, 1);
	//injector::WriteMemory<BYTE>((DWORD)GetModuleHandle("xlive.dll") + 0x105879, 0xEB, 1);
	//injector::MakeNOP(baseAddressxlive + 0x105879, 0x2);
	//injector::MakeNOP(baseAddressxlive + 0x10587A, 0x1);
}
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*lpReserved*/)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
		INI<> ini("ZolikaPatch.ini", true);
		fini_string_t str1 = "Options";
		ini.select(str1);
		base = (DWORD)GetModuleHandle(NULL);
		baseAddressxlive = (unsigned long long)GetModuleHandle("xlive.dll");
		int gameversion = -1;
		switch (*(int*)(base + 0x1230 + 2))
		{
		case 0x00E70678:
			gameversion = 0;
			break;
		case 0xC200050D:
			gameversion = 4;
			break;
		case 0x6868006A:
			gameversion = 5;
			break;
		case 0x3868006A:
			gameversion = 6;
			break;
		case 0xC2001A99:
			gameversion = 7;
			break;
		case 0xC068006A:
			gameversion = 8;
			break;
		case 0x3068006A:
			gameversion = 12;
			break;
		case 0xC2001A6B:
			gameversion = 13;
			break;
		default:
			break;
		}
		//stop it. get some help.
		//if (*(int*)(baseAddress + 0x1230 + 2) == 0x00E70678) gameversion = 0;
		//if (*(int*)(baseAddress + 0x1230 + 2) == 0xC200050D) gameversion = 4;
		//if (*(int*)(baseAddress + 0x1230 + 2) == 0x6868006A) gameversion = 5;
		//if (*(int*)(baseAddress + 0x1230 + 2) == 0x3868006A) gameversion = 6;
		//if (*(int*)(baseAddress + 0x1230 + 2) == 0xC2001A99) gameversion = 7;
		//if (*(int*)(baseAddress + 0x1230 + 2) == 0xC068006A) gameversion = 8;
		//if (*(int*)(baseAddress + 0x1230 + 2) == 0x3068006A) gameversion = 12;
		//if (*(int*)(baseAddress + 0x1230 + 2) == 0xC2001A6B) gameversion = 13;
		// Unprotect image - make .text and .rdata section writeable
		// get load address of the exe
		dwLoadOffset = (DWORD)GetModuleHandle(NULL);
		BYTE * pImageBase = reinterpret_cast<BYTE *>(dwLoadOffset);
		PIMAGE_DOS_HEADER   pDosHeader = reinterpret_cast<PIMAGE_DOS_HEADER> (dwLoadOffset);
		PIMAGE_NT_HEADERS   pNtHeader = reinterpret_cast<PIMAGE_NT_HEADERS> (pImageBase + pDosHeader->e_lfanew);
		PIMAGE_SECTION_HEADER pSection = IMAGE_FIRST_SECTION(pNtHeader);

		for (int iSection = 0; iSection < pNtHeader->FileHeader.NumberOfSections; ++iSection, ++pSection)
		{
			char * pszSectionName = reinterpret_cast<char *>(pSection->Name);
			if (!strcmp(pszSectionName, ".text") || !strcmp(pszSectionName, ".rdata"))
			{
				DWORD dwPhysSize = (pSection->Misc.VirtualSize + 4095) & ~4095;
				DWORD   oldProtect;
				DWORD   newProtect = (pSection->Characteristics & IMAGE_SCN_MEM_EXECUTE) ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
				if (!VirtualProtect(reinterpret_cast <VOID *>(dwLoadOffset + pSection->VirtualAddress), dwPhysSize, newProtect, &oldProtect))
				{
					ExitProcess(0);
				}
			}
		}
		CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)SetStuff, hModule, 0, nullptr);
		//if (std::stoi(ini.get("CEMultiplayerStuff1", "0"), nullptr, 10) == 1)
		//{
		//	injector::MakeJMP(dwLoadOffset + 0x16D0, return1hook);
		//}
		//if (std::stoi(ini.get("CEMultiplayerStuff2", "0"), nullptr, 10) == 1)
		//{
		//	injector::MakeJMP(dwLoadOffset + 0x7DAE30, return1hook);
		//}
#ifdef CEMP
		if (std::stoi(ini.get("CEMultiplayerUnlock", "0"), nullptr, 10) == 1)
		{
			auto pattern = hook::pattern("68 F9 A1 89 2C");
			if (pattern.size() > 0)
			{
				*(int*)(pattern.get(0).get<uintptr_t>(-4)) = (int)IS_MULTIPLAYER_ENABLED;
				injector::MakeCALL(hook::get_pattern("E8 ? ? ? ? B9 ? ? ? ? 5E E9 ? ? ? ? CC CC"), runFrame);
				injector::MakeCALL(hook::pattern("83 3F 00 76 ? FF D5 39").count(2).get(1).get<intptr_t>(-48), runFrame);
				//if (wcsstr(GetCommandLine(), L"cl2"))
				//{
				//	// don't whine about game already running for cl2
				//	hook::put(hook::get_pattern("47 54 41 4E 59 2D 30 38 38"), 'ATGN');
				//}
				iat("ws2_32.dll", CfxBind, 2);
				iat("ws2_32.dll", CfxSendTo, 20);

				MH_Initialize();

				MH_CreateHook((LPVOID)get_call((int)hook::get_pattern("E8 ? ? ? ? E8 ? ? ? ? B9 16 00 00 00")), SocketInitHook, (void**)&g_origSocketInit);
				MH_CreateHook(hook::get_pattern("55 8B EC 83 E4 F8 83 EC 18 33 C0"), GetLocalPeerAddressHook, (void**)&g_origGetLocalPeerAddress);
				MH_CreateHook(hook::get_pattern("51 53 56 8B F2 32 DB"), ParsePacketHook, (void**)&g_origParsePacket);
				MH_EnableHook(MH_ALL_HOOKS);
				/*
	hook::call(hook::get_pattern("E8 ? ? ? ? B9 ? ? ? ? 5E E9 ? ? ? ? CC CC"), runFrame);
	hook::call(hook::pattern("83 3F 00 76 ? FF D5 39").count(2).get(1).get<intptr_t>(-48), runFrame);

	if (wcsstr(GetCommandLine(), L"cl2"))
	{
		// don't whine about game already running for cl2
		hook::put(hook::get_pattern("47 54 41 4E 59 2D 30 38 38"), 'ATGN');
	}

	hook::iat("ws2_32.dll", CfxBind, 2);
	hook::iat("ws2_32.dll", CfxSendTo, 20);

	MH_Initialize();
	
	MH_CreateHook(hook::get_call(hook::get_pattern("E8 ? ? ? ? E8 ? ? ? ? B9 16 00 00 00")), SocketInitHook, (void**)&g_origSocketInit);
	MH_CreateHook(hook::get_pattern("55 8B EC 83 E4 F8 83 EC 18 33 C0"), GetLocalPeerAddressHook, (void**)&g_origGetLocalPeerAddress);
	MH_CreateHook(hook::get_pattern("51 53 56 8B F2 32 DB"), ParsePacketHook, (void**)&g_origParsePacket);
	MH_EnableHook(MH_ALL_HOOKS);*/
			}
		}
#endif
		if (std::stoi(ini.get("RemoveRGSCLoginRequirement", "0"), nullptr, 10) == 1)
		{
			// [v1000 - v1007]
			auto pattern = hook::pattern("83 EC 44 A1 ? ? ? ? 33 C4 89 44 24 40 53 8B 5C 24 4C 56 68 ? ? ? ? E8 ? ? ? ? 83 C4");
			if (pattern.size() > 0) injector::MakeJMP(pattern.get(0).get<uintptr_t>(0), RGSCLoginHook);
			// [v1008]
			else if (gameversion == 8) injector::MakeJMP(base + 0x498460, RGSCLoginHook);
		}
		//if (std::stoi(ini.get("ExtendMouseSensitivityRange", "0"), nullptr, 10) == 1)
		//{
		//	if (gameversion == 8)
		//	{
		//		//*(int*)(dwLoadOffset + 0x816FC) = (int)&MOUSE_SENSITIVITY_OVERRIDE;
		//		//*(int*)(dwLoadOffset + 0x3BD5C9) = (int)&MOUSE_SENSITIVITY_OVERRIDE;
		//		//*(int*)(dwLoadOffset + 0x3BD627) = (int)&MOUSE_SENSITIVITY_OVERRIDE;
		//		*(int*)(dwLoadOffset + 0x74D3A5) = (int)&MOUSE_SENSITIVITY_OVERRIDE;
		//		*(int*)(dwLoadOffset + 0x67CBA2) = (int)&MOUSE_SENSITIVITY_OVERRIDE;
		//		*(int*)(dwLoadOffset + 0x6820E1) = (int)&MOUSE_SENSITIVITY_OVERRIDE;
		//		*(int*)(dwLoadOffset + 0x69C0E5) = (int)&MOUSE_SENSITIVITY_OVERRIDE;
		//		mousesensaddr = dwLoadOffset + 0xCFC2E0;
		//		mousesens = 1;
		//	}
		//}
		if (std::stoi(ini.get("RemoveGFWLUpdatePopup", "0"), nullptr, 10) == 1)
		{
			fixgfwlupdate = true;
		}
		if (std::stoi(ini.get("FixPCIDKickbug", "1"), nullptr, 10) == 1)
		{
			FixGFWLPCID();
		}
		if (std::stoi(ini.get("TeleportProtection", "1")) == 1)
		{
			auto pattern = hook::pattern("55 8B EC 83 E4 F8 51 53 8B 5D 08 56 57 6A 08 8D 44 24 10 8B F1 8D 7B 04 50 8B CF C7 44 24 14 00");
			if (pattern.size() > 0)
			{
				injector::MakeRET(pattern.get(0).get<uintptr_t>(0x0), 8);
			}
		}
		if (std::stoi(ini.get("RemoveAllWeaponsProtection", "1")) == 1)
		{
			//2E8627 
			auto pattern = hook::pattern("E9 ? ? ? ? 57 8D 8F ? ? 00 00");
			if (pattern.size() > 0)
			{
				int start = (int)pattern.get(0).get<int>(0x5);
				injector::MakeNOP(start, 0xC);
				//for (int i = start; i < start + 0xC; i++) *(char*)(dwLoadOffset + )
			}
		}
		if (std::stoi(ini.get("PlayerPedFix", "1")) == 1)
		{
			if (gameversion == 8)
			{
				*(char*)(dwLoadOffset + 0x540D35) = 0x90;
				*(char*)(dwLoadOffset + 0x540D36) = 0x90;
				*(char*)(dwLoadOffset + 0x540D37) = 0x90;
				*(char*)(dwLoadOffset + 0x540D38) = 0x90;
				*(char*)(dwLoadOffset + 0x540D39) = 0x90;
				*(char*)(dwLoadOffset + 0x540D3A) = 0x90;
				//jmp_540D6C = dwLoadOffset + 0x540D6C;
				//injector::MakeJMP(dwLoadOffset + 0x540D62, PlayerPedFixAgain);
				//jmp_463341 = dwLoadOffset + 0x463341;
				//jmp_463346 = dwLoadOffset + 0x463346;
				//injector::MakeJMP(dwLoadOffset + 0x463338, PlayerPedFixAgain2);
				//jmp_39A52 = dwLoadOffset + 0x39A52;
				//jmp_39A19 = dwLoadOffset + 0x39A19;
				//injector::MakeJMP(dwLoadOffset + 0x39A10, PlayerPedDeleteFix);
				//jmp_0x9143A = dwLoadOffset + 0x9143A;
				//jmp_9144B = dwLoadOffset + 0x9144B;
				//injector::MakeJMP(dwLoadOffset + 0x91430, PlayerPedFix);
				//jmp_5409DF = dwLoadOffset + 0x5409DF;
				//jmp_540D6C = dwLoadOffset + 0x540D6C;
				//jmp_463341 = dwLoadOffset + 0x463341;
				//jmp_463346 = dwLoadOffset + 0x463346;
				//injector::MakeJMP(dwLoadOffset + 0x5409CE, PlayerPedFix2);
				//injector::MakeJMP(dwLoadOffset + 0x540D62, PlayerPedFix3);
				//injector::MakeJMP(dwLoadOffset + 0x463331, PlayerPedFix4);
			}
		}
		//if (std::stoi(ini.get("TBoGTThingFix", "1")) == 1)
		//{
		//	if (gameversion == 8)
		//	{
		//		jmp_DFE4 = dwLoadOffset + 0xDFE4;
		//		jmp_DFD0 = dwLoadOffset + 0xDFD0;
		//		injector::MakeJMP(dwLoadOffset + 0xDFC1, TBoGTSPinMPThingFix);
		//	}
		//}
		if (std::stoi(ini.get("SuperLODFix", "1")) == 1)
		{
			if (gameversion == 7)
			{
				jz_0x4A3D37 = dwLoadOffset + 0x4A3D37;
				jmp_0x4A3CEF = dwLoadOffset + 0x4A3CEF;
				call_0x58AAE0 = dwLoadOffset + 0x58AAE0;
				injector::MakeJMP(dwLoadOffset + 0x4A3CE9, SuperLODFix);
			}
			if (gameversion == 8)
			{
				jz_0x4A3D37 = dwLoadOffset + 0x540E67; //
				jmp_0x4A3CEF = dwLoadOffset + 0x540E1F; //
				call_0x58AAE0 = dwLoadOffset + 0x4DD2D0; // GET_MODEL_BY_KEY
				injector::MakeJMP(dwLoadOffset + 0x540E19, SuperLODFix); //
			}
		}
		if (std::stoi(ini.get("VSyncFix", "0"), nullptr, 10) == 1)
		{
			if (gameversion != 8 && gameversion != 13)
			{
				auto pattern = hook::pattern("8B 10 50 8B 82 A8 00 00 00 FF D0 83"); // 0x222814
				if (pattern.size() > 0)
				{
					injector::WriteMemory(pattern.get(0).get<uintptr_t>(0xD), &VSYNC_FIX, true);
					//injector::MakeJMP(pattern.get(0).get<uintptr_t>(0x18), pattern.get(0).get<uintptr_t>(0xBA));
				}
				pattern = hook::pattern("8B 08 8B 51 0C 6A 01 6A 00 6A 00 6A 00 6A 00 50 FF D2 8B F0 81 FE 1C 02 76 88 74");
				if (pattern.size() > 0)
				{
					injector::WriteMemory(pattern.get(0).get<uintptr_t>(0x1E), &VSYNC_FIX, true);
					//injector::MakeJMP(pattern.get(0).get<uintptr_t>(0x23), pattern.get(0).get<uintptr_t>(0x3E));
				}
			}
		}
		if (std::stoi(ini.get("ImprovedShaderStreaming", "0"), nullptr, 10) == 1)
		{
			// [v1000 - v1008]
			auto pattern = hook::pattern("33 F6 8B 4C 24 08 8B 06 8B 50 04 51 8B CE FF D2 F6 D8 1B C0 23 C6 5E C3 8B 44 24 0C 8B 74 24"); // 0x8B21F1
			if (pattern.size() > 0)
			{
				injector::MakeCALL(pattern.get(0).get<uintptr_t>(0x55), GTAPedComparisonASM); // simplify string comp function into a check for a 'p' as the 4th character
				injector::MakeJMP(pattern.get(0).get<uintptr_t>(0x62), pattern.get(0).get<uintptr_t>(0xF5)); // jump to vehicle comparisons immediately after as the new function already returns true for all ped shaders
				injector::MakeCALL(pattern.get(0).get<uintptr_t>(0xFD), GTAVehicleComparisonASM); // same as above for 'v' as 4th
				injector::MakeJMP(pattern.get(0).get<uintptr_t>(0x10A), pattern.get(0).get<uintptr_t>(0x486)); // jump to the end of the func immediately after as the new function already returns true for all vehicle shaders
				//injector::MakeCALL(base + 0x8B2246, GTAPedComparisonASM);
				//injector::MakeJMP(base + 0x8B2253, base + 0x8B22E6);
				//injector::MakeCALL(base + 0x8B22EE, GTAVehicleComparisonASM);
				//injector::MakeJMP(base + 0x8B22FB, base + 0x8B2677);
			}
		}
		//if (std::stoi(ini.get("EFLCAsDLC", "0"), nullptr, 10) == 1)
		//{
		//	if (gameversion == 8)
		//	{
		//		injector::MakeCALL(base + 0x48F35D, XLiveContentGetPath);
		//		injector::MakeCALL(base + 0x6EA4E7, XContentCreateEnumerator);
		//		injector::MakeCALL(base + 0x32AA2F, XMarketplaceCreateOfferEnumerator);
		//		injector::MakeCALL(base + 0x32A957, XContentGetMarketplaceCounts);
		//		injector::MakeCALL(base + 0x32A9E0 + 0xA9, XEnumerate);
		//		injector::MakeCALL(base + 0x373A40 + 0xD7, XEnumerate);
		//		injector::MakeCALL(base + 0x374AA0 + 0xA7, XEnumerate);
		//		injector::MakeCALL(base + 0x374B70 + 0x137, XEnumerate);
		//		injector::MakeCALL(base + 0x374EB0 + 0x15C, XEnumerate);
		//		injector::MakeCALL(base + 0x375340 + 0x99, XEnumerate);
		//		injector::MakeCALL(base + 0x5A29F0 - 0x159E, XEnumerate);
		//		injector::MakeCALL(base + 0x6EA430 + 0xD5, XEnumerate);
		//		injector::MakeCALL(base + 0x48F2E0, XLiveContentCreateAccessHandle);
		//		injector::MakeCALL(base + 0x48F290 + 0x30, XLivePBufferAllocate);
		//		injector::MakeCALL(base + 0x48F2FB, XLivePBufferGetByteArray);
		//	}
		//}
		if (std::stoi(ini.get("LaunchWithPlayGTAIV", "0"), nullptr, 10) == 1)
		{
			if (gameversion == 0)
			{
				strcpy_s((char*)(base + 0x9F043C), 16, "PlayGTAIV.exe");
				strcpy_s((char*)(base + 0x98FA0C), 16, "PlayGTAIV.exe");
			}
			if (gameversion == 4)
			{
				strcpy_s((char*)(base + 0x9EA184), 16, "PlayGTAIV.exe");
				strcpy_s((char*)(base + 0x9DD8A0), 16, "PlayGTAIV.exe");
			}
			if (gameversion == 6)
			{
				strcpy_s((char*)(base + 0x9F8F40), 16, "PlayGTAIV.exe");
				strcpy_s((char*)(base + 0xA9D0D8), 16, "PlayGTAIV.exe");
			}
			if (gameversion == 7)
			{
				strcpy_s((char*)(base + 0x9F9F70), 16, "PlayGTAIV.exe");
				strcpy_s((char*)(base + 0xA9E118), 16, "PlayGTAIV.exe");
			}
			if (gameversion == 8)
			{
				strcpy_s((char*)(base + 0xA508F8), 16, "PlayGTAIV.exe");
				strcpy_s((char*)(base + 0xA5A5E4), 16, "PlayGTAIV.exe");
				//*(int*)(base + 0x8FA5E) = (int)launchivoverride;
				//*(int*)(base + 0xD1002) = (int)launchivoverride;
			}
		}
		if ((std::stoi(ini.get("Patch6and7RemoveModChecks", "0"), nullptr, 10) == 1) || (std::stoi(ini.get("Patch7RemoveModChecks", "0"), nullptr, 10) == 1))
		{
			if (gameversion == 6)
			{
				//*(char*)(base + 0x3917) = 0x90;
				*(char*)(base + 0x392E) = 0xEB; // dsound.dll check //
				*(char*)(base + 0x42E7) = 0x90; // mod enabler native
				*(char*)(base + 0x42E8) = 0x90; // mod enabler native
				*(char*)(base + 0x42E9) = 0x90; // mod enabler native
				*(char*)(base + 0x42EA) = 0x90; // mod enabler native
				*(char*)(base + 0x42EB) = 0x90; // mod enabler native
				*(char*)(base + 0x42EC) = 0x90; // mod enabler native
				*(char*)(base + 0x42ED) = 0x90; // mod enabler native
				injector::MakeJMP(base + 0x3982, base + 0x3A09); // all checks in one function
				*(char*)(base + 0x3A14) = 0xEB; // force some check

				//injector::MakeJMP(base + 0x38BC, base + 0x38CE); // more checks
				*(char*)(base + 0x38AC) = 0x00; // force it to set to 0 even if its checked
				*(char*)(base + 0x397E) = 0x00; // force it to set to 0 even if its checked
			}
			if (gameversion == 7)
			{
				//*(char*)(base + 0x3917) = 0x90;
				*(char*)(base + 0x390E) = 0xEB; // dsound.dll check
				*(char*)(base + 0x42C7) = 0x90; // mod enabler native
				*(char*)(base + 0x42C8) = 0x90; // mod enabler native
				*(char*)(base + 0x42C9) = 0x90; // mod enabler native
				*(char*)(base + 0x42CA) = 0x90; // mod enabler native
				*(char*)(base + 0x42CB) = 0x90; // mod enabler native
				*(char*)(base + 0x42CC) = 0x90; // mod enabler native
				*(char*)(base + 0x42CD) = 0x90; // mod enabler native
				injector::MakeJMP(base + 0x3962, base + 0x39E9); // all checks in one function
				*(char*)(base + 0x39F4) = 0xEB; // force some check
				injector::MakeJMP(base + 0x389C, base + 0x38AE); // more checks
				*(char*)(base + 0x388C) = 0x00; // force it to set to 0 even if its checked
				*(char*)(base + 0x395E) = 0x00; // force it to set to 0 even if its checked
			}
		}
		if (std::stoi(ini.get("Patch4RemoveModChecks", "0"), nullptr, 10) == 1)
		{
			fixmoddedfilespatch4 = true;
		}
		/*if (std::stoi(ini.get("SkipLauncher", "0"), nullptr, 10) == 1)
		{
			// process patches
			uintptr_t* jmp_efc = nullptr;
			auto pattern = hook::pattern("68 ? ? ? ? 89 35 ? ? ? ? E8 ? ? ? ? 83 C4 04 84 C0 0F 84");
			if (pattern.size() > 0)
				jmp_efc = pattern.get(0).get<uintptr_t>(0);

			// NOP - RGSC initialization check [v1000 - v1001]
			pattern = hook::pattern("74 EC 38 5E 06 74 E7 68");
			if (pattern.size() > 0)
			{
				injector::MakeNOP(pattern.get(0).get<uintptr_t>(0), 2, true);
				injector::MakeNOP(pattern.get(0).get<uintptr_t>(5), 2, true);
			}

			// NOP - RGSC initialization check [v1002 - v1004]
			pattern = hook::pattern("74 C4 8B 07 3B 46 18 75 BD 68");
			if (pattern.size() > 0)
				injector::MakeNOP(pattern.get(0).get<uintptr_t>(0), 2, true);

			// NOP - last RGSC init check [v1002 - v1004]
			pattern = hook::pattern("3B 56 18 0F 85");
			if (pattern.size() > 0)
				injector::MakeNOP(pattern.get(0).get<uintptr_t>(3), 6, true);

			// NOP - last RGSC init check [v1005]
			pattern = hook::pattern("3B 4E 18 0F 85");
			if (pattern.size() > 0)
				injector::MakeNOP(pattern.get(0).get<uintptr_t>(3), 6, true);

			// NOP - last RGSC init check [v1006 - v1008]
			pattern = hook::pattern("0F 85 ? ? ? ? 8B 8C 24 ? ? ? ? 5F 5E 5B 33 CC C6");
			if (pattern.size() > 0)
				injector::MakeNOP(pattern.get(0).get<uintptr_t>(0), 6, true);

			// RGSC initialization check [v1002 - v1004]
			pattern = hook::pattern("3B 46 18 75 BD");
			if (pattern.size() > 0)
			{
				injector::WriteMemory<uint16_t>(pattern.get(0).get<uintptr_t>(3), 0xC033, true); //XOR eax, eax
				injector::WriteMemory<uint16_t>(pattern.get(0).get<uintptr_t>(10), 0xA390, true); //NOP; MOV [], eax
			}

			// EFC20 [v1003 - v1004]
			pattern = hook::pattern("57 51 C7 44 24 ? ? ? ? ? C7 44 24 ? ? ? ? ? C7 44 24");
			if (pattern.size() > 0)
				injector::MakeNOP(pattern.get(0).get<uintptr_t>(1), 0x1BF, true);

			// fix messed sequences
			{
				pattern = hook::pattern("A1 ? ? ? ? 85 C0 8B 0D ? ? ? ? 75");
				if (pattern.size() > 0)
				{
					injector::WriteMemory(pattern.get(0).get<uintptr_t>(0), 0x90C301B0, true); // 0xBAC160  mov al, 1; retn
					injector::WriteMemory(pattern.get(1).get<uintptr_t>(0), 0x90C301B0, true); // 0xBAC190  mov al, 1; retn
				}

				pattern = hook::pattern("33 C0 83 3D ? ? ? ? 01 0F 94 C0 C3");
				if (pattern.size() > 0)
				{
					injector::WriteMemory(pattern.get(pattern.size() - 2).get<uintptr_t>(0), 0x90C301B0, true); // 0xBAC180  mov al, 1; retn
					injector::WriteMemory(pattern.get(pattern.size() - 1).get<uintptr_t>(0), 0x90C301B0, true); // 0xBAC1C0  mov al, 1; retn
				}
			}

			// skip RGSC connect and EFC checks [v1005 - v1008]
			pattern = hook::pattern("8B 56 1C 3B 56 20 ? ? 6A 00 6A 00");
			if (pattern.size() > 0 && jmp_efc != nullptr)
			{
				injector::WriteMemory<uint16_t>(pattern.get(0).get<uintptr_t>(0), 0xC033, true); // xor eax, eax - address of the RGSC object
				injector::MakeJMP(pattern.get(0).get<uintptr_t>(2), jmp_efc, true);
			}

			// NOP; MOV [g_rgsc], eax [v1000 - v1008]
			pattern = hook::pattern("89 35 ? ? ? ? E8 ? ? ? ? 83 C4 04 84 C0");
			if (pattern.size() > 0)
			{
				injector::WriteMemory<uint16_t>(pattern.get(0).get<uintptr_t>(0), 0xA390, true);
			}

			//skip missing tests [v1005]
			{
				pattern = hook::pattern("8B 50 2C 3B 50 30");
				if (pattern.size() > 0)
				{
					injector::WriteMemory<uint16_t>(pattern.get(0).get<uintptr_t>(0), 0xC033, true);
					injector::MakeNOP(pattern.get(0).get<uintptr_t>(2), 4, true);
				}

				pattern = hook::pattern("8B 48 2C 83 EC 78");
				if (pattern.size() > 0)
				{
					injector::MakeNOP(pattern.get(0).get<uintptr_t>(0), 3, true);
					injector::MakeNOP(pattern.get(0).get<uintptr_t>(6), 11, true);

				}

				pattern = hook::pattern("8B 48 2C 83 EC 74");
				if (pattern.size() > 0)
				{
					injector::MakeNOP(pattern.get(0).get<uintptr_t>(0), 3, true);
					injector::MakeNOP(pattern.get(0).get<uintptr_t>(6), 11, true);

				}

				pattern = hook::pattern("8B 48 2C 3B 48 30 74 03");
				if (pattern.size() > 0)
					injector::MakeNOP(pattern.get(0).get<uintptr_t>(0), 11, true);

				pattern = hook::pattern("8B 48 2C 3B 48 30 53");
				if (pattern.size() > 0)
					injector::MakeNOP(pattern.get(0).get<uintptr_t>(0), 32, true);

				pattern = hook::pattern("8B 48 2C 3B 48 30 74 11");
				if (pattern.size() > 0)
					injector::MakeNOP(pattern.get(0).get<uintptr_t>(0), 25, true);

				pattern = hook::pattern("8B 48 2C 3B 48 30 75 DC 8D 54 24 18");
				if (pattern.size() > 0)
					injector::MakeNOP(pattern.get(0).get<uintptr_t>(0), 8, true);

				pattern = hook::pattern("8B 48 2C 3B 48 30 75 DC 8D 54 24 1C");
				if (pattern.size() > 0)
					injector::MakeNOP(pattern.get(0).get<uintptr_t>(0), 8, true);

				pattern = hook::pattern("8B 0D ? ? ? ? 8B 51 2C");
				if (pattern.size() > 0)
					injector::MakeNOP(pattern.get(0).get<uintptr_t>(0), 14, true);
			}

			//skip missing tests [v1003 - v1005]
			pattern = hook::pattern("51 53 55 56 57 8B 3D ? ? ? ? 8B 47 2C");
			if (pattern.size() > 0)
				injector::MakeRET(pattern.get(0).get<uintptr_t>(0), 0, true);

			//skip missing tests [v1005 - v1008]
			pattern = hook::pattern("3B C7 0F 85 ? ? ? ? 85 C0");
			if (pattern.size() > 0)
				injector::MakeNOP(pattern.get(0).get<uintptr_t>(0), 16, true);

			//skip missing tests [v1005 - v1008]
			pattern = hook::pattern("0F 85 ? ? ? ? 85 C0 0F 84 ? ? ? ? E8 ? ? ? ? 85 D2");
			if (pattern.size() > 0)
				injector::MakeNOP(pattern.get(0).get<uintptr_t>(0), 14, true);

			// skip missing tests [v1006 - v1007]
			pattern = hook::pattern("83 EC 24 A1 ? ? ? ? 8B 48 14 53 55 56 57");
			if (pattern.size() > 0)
				injector::WriteMemory(pattern.get(0).get<uintptr_t>(0), 0x90C3C033, true);

			// skip missing tests [v1006 - v1007]
			pattern = hook::pattern("83 EC 08 8B 0D ? ? ? ? 8B 51 14 8D 04 24 50 6A 00 52");
			if (pattern.size() > 0)
				injector::WriteMemory(pattern.get(0).get<uintptr_t>(0), 0x90C3C033, true);

			//[v1006 - v1008]
			pattern = hook::pattern("E8 ? ? ? ? 83 C4 0C 83 F8 01 74 11 6A 00 6A 00");
			if (pattern.size() > 0)
				injector::MakeCALL(pattern.get_first(0), dfaInit, true);

			//[v1005 - v1008]
			pattern = hook::pattern("55 8B EC 6A 00 E8");
			if (pattern.size() > 0)
				injector::MakeJMP(pattern.get(0).get<uintptr_t>(0), CreateFileHook, true);

			//[v1005 - v1008]
			pattern = hook::pattern("6A 00 E8 ? ? ? ? 83 F8 01 59 75 17 FF 15 ? ? ? ? 50 B9 ? ? ? ? E8 ? ? ? ? 8B 40 08 FF 60 0C 33 C0 C3");
			if (pattern.size() > 0)
				injector::MakeJMP(pattern.get(0).get<uintptr_t>(0), CloseHandleHook, true);

			//[v1005 - v1008]
			pattern = hook::pattern("6A 00 E8 ? ? ? ? 83 F8 01 59 75 17 FF 15 ? ? ? ? 50 B9 ? ? ? ? E8 ? ? ? ? 8B 40 08 FF 60 ? 83 C8 FF C3");
			if (pattern.size() > 0)
				injector::MakeJMP(pattern.get(0).get<uintptr_t>(0), SetFilePointerHook, true);

			//[v1005 - v1008]
			pattern = hook::pattern("56 8B 74 24 0C 8D 46 04 50 FF 74 24 0C E8 ? ? ? ? 59 59 33 C9 83 F8 FF 0F 95 C1 89 06 5E 8B C1 C3");
			if (pattern.size() > 0)
				injector::MakeJMP(pattern.get(0).get<uintptr_t>(0), GetFileSizeExHook, true);

			//[v1005 - v1008]
			pattern = hook::pattern("55 8B EC 51 51 53 56 8B C5");
			if (pattern.size() > 0)
				injector::MakeJMP(pattern.get(0).get<uintptr_t>(0), ReadFileHook, true);

			//[v1006 - v1008]
			pattern = hook::pattern("A1 ? ? ? ? B9 A7 FA DC 5C F7 E1 8B 0D ? ? ? ? 33 F6");
			if (pattern.size() > 0)
			{
				injector::MakeJMP(pattern.get(0).get<uintptr_t>(0), hook::pattern("80 BC 24 ? ? ? ? ? 6A 00").get(0).get<uintptr_t>(0), true);
				injector::MakeJMP(pattern.get(1).get<uintptr_t>(0), hook::pattern("6A 00 68 80 00 00 00 6A 03 6A 00 6A 01 68 00 00 00 80 57").get(0).get<uintptr_t>(0), true);
			}

			// windows message id [v1000 - v1008]
			//pattern = hook::pattern("A1 ? ? ? ? 8B 0D ? ? ? ? 68 ? ? ? ? 53 50 51 FF 15");
			//injector::WriteMemory(*pattern.get_first<uint32_t>(1), WM_ROM, true);
			//injector::WriteMemory(*pattern.get_first<uint32_t>(7), CreateROMWindow(), true); // window handle

			// SendMessage call to this window that ends up freezing the audio update thread and resulting in a deadlock [v1000 - v1008]
			pattern = hook::pattern("FF 15 ? ? ? ? E8 ? ? ? ? E8 ? ? ? ? E8 ? ? ? ? E8 ? ? ? ? B9 ? ? ? ? E8 ? ? ? ? B9 ? ? ? ? E8 ? ? ? ? 83 7C 24");
			injector::MakeNOP(pattern.get_first(0), 6, true);
			injector::MakeCALL(pattern.get_first(0), SendMessageFakie, true);
		}
		*/
		if (std::stoi(ini.get("NoLoadingSleep", "0"), nullptr, 10) == 1)
		{
			// disable sleep [v1002 - v1008]
			auto pattern = hook::pattern("68 88 13 00 00 FF 15");
			if (pattern.size() > 0)
				injector::WriteMemory(pattern.get(0).get<uintptr_t>(1), 1, true);
		}
		if (std::stoi(ini.get("SkipIntro", "1"), nullptr, 10) == 1)
		{
			auto pattern = hook::pattern("89 91 ? ? ? ? 8D 44 24 ? 68 ? ? ? ? 50");
			struct Loadsc
			{
				void operator()(injector::reg_pack& regs)
				{
					*(int32_t*)&regs.ecx *= 400;
					if (regs.edx < 8000)
						regs.edx = 0;
				}
			}; injector::MakeInline<Loadsc>(pattern.get_first(-6), pattern.get_first(0));
		}
		if (std::stoi(ini.get("SkipMenu", "0"), nullptr, 10) == 1)
		{
			auto pattern = hook::pattern("83 F8 03 75 ? A1 ? ? ? ? 80 88 ? ? ? ? ? 84 DB 74 0A 6A 00 E8 ? ? ? ? 83 C4 04 5F 5E");
			if (pattern.size() > 0)
			{
				// [v1002 - v1008]
				hbsub_7870A0.fun = injector::MakeCALL(pattern.get_first(23), sub_7870A0).get();
			}
			else
			{
				// [v1000]
				pattern = hook::pattern("6A 00 E8 ? ? ? ? 83 C4 04 8B 8C 24 ? ? ? ? 5F 5E 5D 5B 33 CC E8 ? ? ? ? 81 C4 ? ? ? ? C3");
				if (pattern.size() > 0)
					hbsub_7870A0.fun = injector::MakeCALL(pattern.get(0).get<uintptr_t>(2), sub_7870A0).get();
				else
				{
					// [v1001]
					pattern = hook::pattern("6A 00 E8 ? ? ? ? 83 C4 04 8B 8C 24 ? ? ? ? 5F 5E 5B 33 CC E8 ? ? ? ? 8B E5 5D C3");
					if (pattern.size() > 0)
						hbsub_7870A0.fun = injector::MakeCALL(pattern.get(0).get<uintptr_t>(2), sub_7870A0).get();
				}
			}
		}
		if (std::stoi(ini.get("MouseFix", "1"), nullptr, 10) == 1)
		{
			auto pattern = hook::pattern("51 8B 44 24 0C 85 C0 0F 57 D2 56 8B 35 ? ? ? ? 57 8B 3D ? ? ? ? F3 0F 11 54 24 08 75 0C");
			if (pattern.size() > 0)
			{
				ADDRESS_MOUSE_X = *(int*)pattern.get(0).get<unsigned int>(13);
				ADDRESS_MOUSE_Y = *(int*)pattern.get(0).get<unsigned int>(20);
				injector::MakeJMP(pattern.get(0).get<uintptr_t>(0), mousefixhookpattern);
			}
		}
		if (std::stoi(ini.get("DoNotPauseOnMinimize", "0"), nullptr, 10) == 1)
		{
			// [v1002 - v1008]
			auto pattern = hook::pattern("75 ? 8B 0D ? ? ? ? 51 FF 15 ? ? ? ? 85 C0 75 ? 8B 15"); //0x402D5A
			if (pattern.size() > 0)
				injector::MakeNOP(pattern.get(0).get<uintptr_t>(0), 2, true);
		}
		if (std::stoi(ini.get("BorderlessWindowed", "1"), nullptr, 10) == 1)
		{
			// [v1000 - v1008]
			auto pattern = hook::pattern("FF 15 ? ? ? ? 5F 5E 5D 5B 83 C4 10 C2 10 00");
			injector::MakeNOP(pattern.count(2).get(0).get<void>(0), 6, true);
			injector::MakeCALL(pattern.count(2).get(0).get<void>(0), DefWindowProcWProxy, true);
			injector::MakeNOP(pattern.count(2).get(1).get<void>(0), 6, true);
			injector::MakeCALL(pattern.count(2).get(1).get<void>(0), DefWindowProcAProxy, true);

			//// directinput fix
			//pattern = hook::pattern("C7 44 24 14 10 00 00 00 89 5C 24 1C 89 5C 24 18");
			//*(char*)(pattern.get(0).get<uintptr_t>(-0x29)) = 6;
				//.text:001B68D0	sub_1B6820	mov     [esp+24h+var_10], 10h

		}
		if (std::stoi(ini.get("VRAMFix", "1"), nullptr, 10) == 1)
		{
			// workaround for vram detection, so settings won't be locked out [v1000 - v1007]
			auto pattern = hook::pattern("E8 ? ? ? ? D9 E8 85 C0");
			if (pattern.size() > 0)
				hbGetVidMem.fun = injector::MakeCALL(pattern.get_first(0), GetVidMem).get();

			pattern = hook::pattern("E8 ? ? ? ? F3 0F 10 0D ? ? ? ? F3 0F 5C 0D");
			if (pattern.size() > 0)
				hbGetVidMem.fun = injector::MakeCALL(pattern.get_first(0), GetVidMem).get();

			pattern = hook::pattern("E8 ? ? ? ? F3 0F 10 05 ? ? ? ? 89 44 24 08 6A 01 8D 44 24");
			if (pattern.size() > 0)
				hbGetVidMem.fun = injector::MakeCALL(pattern.get_first(0), GetVidMem).get();
		}
		if (std::stoi(ini.get("SavegameFix", "0"), nullptr, 10) == 1)
		{
			//auto pattern = hook::pattern("8A 10 83 C0 01 84 D2 75 F7 56 57 BF ? ? ? ? 2B C1 8B F1 83 C7 FF 8A 4F 01 83 C7 01");
			//if (pattern.count(3).size() > 0)
			//{
				//auto range_end = (uintptr_t)pattern.get(2).get<uintptr_t>(0);
				//auto tmp = hook::pattern(range_end - 0x80, range_end, "8A 88 ? ? ? ? 88 88 ? ? ? ? 83 C0 01");
				//char *pszPath = *tmp.get(0).get<char*>(2);
		
				auto pattern = hook::pattern("75 ? 83 C1 01 83 C0 01 83 F9 04"); // 21EDB5
				if (pattern.size() > 0)
				{
					injector::MakeNOP(pattern.get(0).get<uintptr_t>(0), 2, true); // NOP - save file CRC32 check
					injector::MakeCALL(pattern.get(0).get<uintptr_t>(-0x49), XLiveCloseProtectedDataContext); // xlive_5038 21ED6C
					injector::MakeCALL(pattern.get(0).get<uintptr_t>(-0x8F), XLiveUnprotectData); // xlive_5035 21ED26
					injector::MakeCALL(pattern.get(0).get<uintptr_t>(-0xB0), XLiveUnprotectData); // xlive_5035 21ED05
					injector::MakeCALL(pattern.get(0).get<uintptr_t>(-0x118), XLiveCreateProtectedDataContext); // xlive_5036 21EC9D
					injector::MakeCALL(pattern.get(0).get<uintptr_t>(-0xF0), XLiveQueryProtectedDataInformation); // xlive_5037 21ECC5
				}
				// xlive_5038: 21ED6C
				// xlive_5035: 21ED26 21ED05
				// xlive_5036: 21EC9D
				// xlive_5037: 21ECC5
		
				pattern = hook::pattern("8D 4C 24 18 51 6A 00 6A 0C 57"); // 21EBA1
				if (pattern.size() > 0)
				{
					injector::MakeCALL(pattern.get(0).get<uintptr_t>(-0x56), XLiveCreateProtectedDataContext); // xlive_5036 21EB4B
					injector::MakeCALL(pattern.get(0).get<uintptr_t>(-0x2F), XLiveQueryProtectedDataInformation); // xlive_5037 21EB72
					injector::MakeCALL(pattern.get(0).get<uintptr_t>(0xA), XLiveProtectData); // xlive_5034 21EBAB
					injector::MakeCALL(pattern.get(0).get<uintptr_t>(0x5A), XLiveProtectData); // xlive_5034 21EBFB
					injector::MakeCALL(pattern.get(0).get<uintptr_t>(0x80), XLiveCloseProtectedDataContext); // xlive_5038 21EC21
				}
				// xlive_5036: 21EB4B
				// xlive_5037: 21EB72
				// xlive_5034: 21EBAB 21EBFB
				// xlive_5038: 21EC21

				//pattern = hook::pattern("55 8B EC 83 E4 F8 83 EC 4C 56");
				//if (pattern.size() > 0)
				//	injector::MakeJMP(pattern.get(0).get<uintptr_t>(0), getSavefilePath, true); // replace getSavefilePath
			//}
		}
		if (std::stoi(ini.get("MiscFixes", "1"), nullptr, 10) == 1)
		{
			// data integrity checks VDS102 [v1002 - v1004]
			auto pattern = hook::pattern("C7 06 ? ? ? ? 74 19");
			if (pattern.size() > 0)
				injector::WriteMemory<uint8_t>(pattern.get(0).get<uintptr_t>(6), 0xEB, true); //jmp

			// data integrity checks VDS102 [v1005 - v1008]
			pattern = hook::pattern("83 C4 ? 3B 46 20");
			if (pattern.size() > 0)
				injector::WriteMemory<uint8_t>(pattern.get(0).get<uintptr_t>(12), 0xEB, true); //jmp

			// data integrity checks VDS100 [v1006 - v1008]
			pattern = hook::pattern("75 28 6A 00 6A 00 68 ? ? ? ? E8 ? ? ? ? 83 C4 0C");
			if (pattern.size() > 0)
				injector::WriteMemory<uint8_t>(pattern.get(0).get<uintptr_t>(0), 0xEB, true); //jmp

			// token activation [v1006 - v1007]
			pattern = hook::pattern("8B 54 24 04 52 A3");
			if (pattern.size() > 0)
				injector::WriteMemory<uint16_t>(pattern.get(0).get<uintptr_t>(0), 0xC9EB, true);

			// token activation [v1006 - v1007]
			pattern = hook::pattern("83 F8 01 75 30 A3");
			if (pattern.size() > 0)
			{
				injector::WriteMemory<uint8_t>(pattern.get(0).get<uintptr_t>(0), 0xB8, true);
				injector::WriteMemory(pattern.get(0).get<uintptr_t>(1), 1, true);
			}

			// skip first-time play ask [v1006 - v1008]
			pattern = hook::pattern("C7 05 ? ? ? ? 03 00 00 00 33 C0 5E 59 C3 C7");
			if (pattern.size() > 0)
				injector::WriteMemory<uint16_t>(pattern.get(0).get<uintptr_t>(0), 0x0DEB, true);

			// [v1000 - v1001]
			pattern = hook::pattern("FF 15 ? ? ? ? 85 C0 75 18");
			if (pattern.size() > 0)
				injector::MakeNOP(pattern.get(0).get<uintptr_t>(8), 2, true); //InternetGetConnectedState

			// [v1002 - v1005]
			pattern = hook::pattern("FF 15 ? ? ? ? 85 C0 75 04 32");
			if (pattern.size() > 0)
				injector::MakeNOP(pattern.get(0).get<uintptr_t>(8), 2, true); //InternetGetConnectedState

			// [v1006 - v1008]
			pattern = hook::pattern("FF 15 ? ? ? ? 85 C0 75 07 A0");
			if (pattern.size() > 0)
				injector::MakeNOP(pattern.get(0).get<uintptr_t>(8), 2, true); //InternetGetConnectedState

			// [v1003 - v1008]
			pattern = hook::pattern("81 EC ? ? ? ? A1 ? ? ? ? 33 C4 89 84 24 ? ? ? ? 53 ? 6A 3C 33 DB 8D 44 24 ? 53 50 E8"); // health check
			if (pattern.size() > 0)
				injector::WriteMemory<uint8_t>(pattern.get(0).get<uintptr_t>(0), 0xC3, true);

			if (gameversion != 12 && gameversion != 13)
			{
				//[v1006 - v1008]
				pattern = hook::pattern("E8 ? ? ? ? 83 C4 0C 83 F8 01 74 11 6A 00 6A 00");
				if (pattern.size() > 0)
					injector::MakeCALL(pattern.get_first(0), dfaInit, true);

				//[v1005 - v1008]
				pattern = hook::pattern("55 8B EC 6A 00 E8");
				if (pattern.size() > 0)
					injector::MakeJMP(pattern.get(0).get<uintptr_t>(0), CreateFileHook, true);

				//[v1005 - v1008]
				pattern = hook::pattern("6A 00 E8 ? ? ? ? 83 F8 01 59 75 17 FF 15 ? ? ? ? 50 B9 ? ? ? ? E8 ? ? ? ? 8B 40 08 FF 60 0C 33 C0 C3");
				if (pattern.size() > 0)
					injector::MakeJMP(pattern.get(0).get<uintptr_t>(0), CloseHandleHook, true);

				//[v1005 - v1008]
				pattern = hook::pattern("6A 00 E8 ? ? ? ? 83 F8 01 59 75 17 FF 15 ? ? ? ? 50 B9 ? ? ? ? E8 ? ? ? ? 8B 40 08 FF 60 ? 83 C8 FF C3");
				if (pattern.size() > 0)
					injector::MakeJMP(pattern.get(0).get<uintptr_t>(0), SetFilePointerHook, true);

				//[v1005 - v1008]
				pattern = hook::pattern("56 8B 74 24 0C 8D 46 04 50 FF 74 24 0C E8 ? ? ? ? 59 59 33 C9 83 F8 FF 0F 95 C1 89 06 5E 8B C1 C3");
				if (pattern.size() > 0)
					injector::MakeJMP(pattern.get(0).get<uintptr_t>(0), GetFileSizeExHook, true);

				//[v1005 - v1008]
				pattern = hook::pattern("55 8B EC 51 51 53 56 8B C5");
				if (pattern.size() > 0)
					injector::MakeJMP(pattern.get(0).get<uintptr_t>(0), ReadFileHook, true);

				//[v1006 - v1008]
				pattern = hook::pattern("A1 ? ? ? ? B9 A7 FA DC 5C F7 E1 8B 0D ? ? ? ? 33 F6");
				if (pattern.size() > 0)
				{
					injector::MakeJMP(pattern.get(0).get<uintptr_t>(0), hook::pattern("80 BC 24 ? ? ? ? ? 6A 00").get(0).get<uintptr_t>(0), true);
					injector::MakeJMP(pattern.get(1).get<uintptr_t>(0), hook::pattern("6A 00 68 80 00 00 00 6A 03 6A 00 6A 01 68 00 00 00 80 57").get(0).get<uintptr_t>(0), true);
				}
			}
		}
    }
    return TRUE;
}