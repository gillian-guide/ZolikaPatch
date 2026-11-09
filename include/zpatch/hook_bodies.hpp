#pragma once
// Portable reimplementations of the ZolikaPatch 7.65 hook bodies recovered in
// ZolikaPatch_analysis/checkpoint_0002_trampolines (see trampoline_bodies.md).
//
// The original writes E9 jumps from game code into these bodies; the bodies
// run ASI logic and resume the displaced game code through runtime-filled
// .data slots. This module reproduces the behaviour of each body with the same
// inputs and the same side effects, with every game interaction made explicit
// through GameCallSlots instead of raw machine code. Nothing here performs OS
// calls or touches a live process; callers own all memory.
//
// Naming: each function carries the recovered reference VA of the original
// body; slot fields carry the original .data slot VA they mirror.

#include <cstddef>
#include <cstdint>

namespace zpatch::hook_bodies {

// ---------------------------------------------------------------------------
// Runtime call slots.
//
// One member per original `.data` slot a recovered body calls through
// (jmp ds:[slot] / call ds:[slot]). The original fills these while installing
// hooks; a rebuilt ASI does the same. A null member means "not installed":
// bodies treat it as a no-op and report it where a status is available (the
// original would crash; making it testable is the point of this module).
// ---------------------------------------------------------------------------
struct GameCallSlots final {
  // Vehicle animation detour (detour_100051B0, installed by sub_10005210).
  void (*vehicle_anim_resume_1015AD44)(){};      // resume displaced game code

  // EmissiveLerpFix (tramp_EmissiveLerpFix 0x1001af60).
  int (*emissive_lerp_resume_10159CC8)(int){};   // ecx = stored 0x10159CC0

  // BadPlayerModelCrashFix (tramp_BadPlayerModelCrashFix 0x10037ba0).
  int (*bad_player_model_resume_10159CB8)(int, int){};

  // BenchmarkFix (sub_10005650 / sub_10005670).
  int (*benchmark_resume_a_1015AD50)(){};
  int (*benchmark_resume_b_1015AD54)(){};

  // BuildingDynamicShadows (sub_10005AF0 0x10005af0).
  int (*building_shadow_default_10159C20)(){};
  int (*building_shadow_special_10159C28)(){};

  // CollisionDeadlockFix (sub_10005D80 0x10005d80).
  unsigned char (*collision_blocked_1015BDD4)(int){};  // arg: 1 << (flags & 3)

  // CopSirensFlag (tramp_CopSirensFlag_a 0x10006310, sub_10006330 0x10006330).
  int (*cop_sirens_resume_1015937C)(int){};  // resumed code reads the eax value
  int (*cop_sirens_resume_args_10159374)(int, int){};

  // DrivebyFOVFix (sub_100064B0 0x100064b0).
  int (*driveby_fov_resume_1015BDF4)(int, int){};

  // CarPartsShadowFix (sub_10006880 0x10006880).
  int (*car_parts_shadow_null_1015BDC0)(){};
  int (*car_parts_shadow_present_1015BDC8)(){};

  // detour_100068A0.
  void (*detour_100068A0_step_1015B5B8)(){};
  int (*detour_100068A0_resume_1015BDCC)(){};

  // EpisodeSwitchFreezeFix (0x1001b040..0x1001b0c0).
  int (*episode_switch_resume_10159BBC)(int, int, int, int){};
  int (*episode_switch_resume_b_10159BB4)(){};
  int (*episode_switch_resume_c_10159BB8)(){};

  // HighFPSBikePhysicsFix (tramp 0x1001ef60, loop body 0x1001f030).
  int (*bike_physics_resume_1015AD84)(int, float){};   // ecx = esi, float arg
  int (*bike_physics_step_10159D04)(int, int, int){};  // (phase, arg_a, arg_b)
  int (*bike_physics_loop_resume_10159CFC)(){};        // tail jump after phase 11

  // GroupHackFix (sub_10036E90 0x10036e90).
  int (*group_hack_resume_1015AD60)(){};

  // FreezeGunFix (sub_10036EB0 0x10036eb0).
  int (*freeze_gun_resume_1015AD64)(int, int){};

  // BlackscreenFix (sub_10036EE0 0x10036ee0).
  int (*blackscreen_resume_1015AD58)(){};

  // CrashMsgFix (sub_10036F00 0x10036f00).
  char* (*crash_msg_message_1015AD68)(){};

  // LoadDLCs (sub_10023490 0x10023490).
  void (*dlc_load_callback_1015BDDC)(void*){};  // this = struct pointer

  // PathNodeCrashFix (sub_1003D7B0 0x1003d7b0).
  int (*path_node_resume_special_1015BDFC)(){};
  int (*path_node_resume_1015BE00)(){};

  // PlaneCrashFix (sub_1003D870 0x1003d870).
  int (*plane_resume_1015BE14)(){};
  int (*plane_resume_null_1015BE18)(){};

  // PlayerAsPedComponentsFix (sub_1003D9A0 0x1003d9a0).
  int (*player_ped_components_resume_10159D2C)(){};

  // PopupHeadlightsFlag (sub_10044560 0x10044560).
  int (*popup_headlights_resume_a_1015D628)(int, int){};
  int (*popup_headlights_resume_b_1015D62C)(int, int){};

  // SMPA60Fix (tramp 0x10046750, sub_10046780 0x10046780).
  int (*smpa60_resume_10159C68)(int, int, int, int, int){};
  int (*smpa60_resume_marker_10159C70)(){};
  int (*smpa60_resume_tail_10159C6C)(){};

  // SuperLODFix (tramp_SuperLODFix 0x100469e0).
  int (*super_lod_skip_101580AC)(){};
  int (*super_lod_resume_101580B0)(){};

  // WantedLevelCrashFix (sub_10050490 0x10050490).
  int (*wanted_level_resume_1015AD6C)(){};

  // NewSavesCompatibility (sub_100521E0 0x100521e0).
  int (*new_saves_resume_1015AD88)(int, int){};

  // NoLiveryLimit (tramp 0x1003d310..0x1003d3e0).
  int (*no_livery_resume_10159B9C)(int, int){};
  int (*no_livery_resume_tail_10159BAC)(int, int){};

  // SavegameFix (sub_100455E0 0x100455e0).
  void* (*create_mutex)(){};  // CreateMutexA(NULL, FALSE, NULL)

  // NewFilesCompatibility (sub_10051890 / sub_100518D0).
  unsigned int (*hash_name)(const char*){};            // sub_10003E50
  void* (*dictionary_container)(){};                   // sub_10051730
  bool (*dictionary_contains)(void*, unsigned int){};  // sub_10051610
  void (*dictionary_insert)(void*, const char*){};     // sub_100517E0
};

// ---------------------------------------------------------------------------
// Class A - replacement bodies (whole-function takeover).
// ---------------------------------------------------------------------------

// 0x10005590 AimProjectileCrashFix.
// if (this) { this[8]=a3; this[9]=a4; this[7]=a4?a2:0; }
int aim_projectile_crash_fix(void* self, int a2, int a3, int a4) noexcept;

// 0x100057f0 BetterMPSync.
int better_mp_sync() noexcept;

// 0x10005ed0 CombinedRadioFreezeFix.
int combined_radio_freeze_fix() noexcept;

// 0x10023480 LoadDLCs (unconditional-success replacement).
int load_dlcs_dummy() noexcept;

// 0x10038550 RemoveRGSCLoginRequirement.
int rgsc_login_removed() noexcept;

// 0x10038570 SavegameFix (unconditional-success replacement).
int savegame_fix_null() noexcept;

// 0x10045580 SavegameFix (CloseHandle replacement). Returns 0.
int savegame_fix_close(void* handle, void (*close_handle)(void*)) noexcept;

// 0x100455e0 SavegameFix (CreateMutex replacement). Returns 0; writes handle.
int savegame_fix_mutex(void* out_handle, void* (*create_mutex)()) noexcept;

// 0x10005890 / 0x10005900 BorderlessWindowed style computation.
struct BorderlessStyles final {
  std::int32_t style{};
  std::int32_t ex_style{};
};
BorderlessStyles borderless_style_fixup(std::int32_t style, std::int32_t ex_style) noexcept;

// 0x10045a30 / 0x10045a80 ImprovedShaderStreaming.
// Entries hold shader objects; the name getter (vtable +0x10) returns a name
// pointer; the filter (sub_100459E0 / sub_10045A10) receives (query, name) and
// returns 0 when the entry matches (both recovered filters return 0 for a
// name whose byte at +4 is 'h' / "pe" / 'v'). Returns the first matching entry
// or null when none matches.
using ShaderStreamingNameFn = const void* (*)(void*);
using ShaderStreamingFilterFn = int (*)(int, const void*);
void* improved_shader_streaming_find(void* const* entries, std::uint16_t count,
                                     int query, ShaderStreamingNameFn get_name,
                                     ShaderStreamingFilterFn filter) noexcept;

// 0x10050540 SpeedupBugFix timing constants (from QueryPerformanceFrequency f):
// seconds_per_tick = 1000/f; per_tick_ms = 1e6/f; ticks_per_second = f/1e6;
// per_tick_seconds = (1000/f)*0.001 (all through float, matching the stores).
struct SpeedupTimingConstants final {
  float seconds_per_tick{};
  float per_tick_ms{};
  float ticks_per_second{};
  float per_tick_seconds{};
};
SpeedupTimingConstants speedup_timing_constants(std::int64_t qpc_frequency) noexcept;

// 0x10050870 PedModelsInCutscenesFix.
// Compares *(int16*)(object+0x2E) against the index resolved for "player"
// through hash + lookup. Returns whether the model matches.
bool ped_models_in_cutscenes_check(const void* object, unsigned int (*hash)(const char*),
                                   void (*lookup)(unsigned int, int*)) noexcept;

// 0x10051890 / 0x100518D0 NewFilesCompatibility.
// `name_field` points at the string (object+0x10 / object+0x10F); `insert_target`
// is the sub-object the insert call receives (object+8 / object+4). Returns the
// original's return value (false when the field is empty, true when handled).
bool new_files_compat_update(const char* name_field, void* insert_target,
                             GameCallSlots& slots) noexcept;

// 0x1001b080 EpisodeSwitchFreezeFix (this[3] + this[4]).
int episode_switch_sum(const void* self) noexcept;

// 0x10038670 NewMemorySystem (vtable patch into allocator object).
// Returns the allocator pointer; writes `vtable` at *allocator when non-null.
void* new_memory_system_apply(void* allocator, void* vtable) noexcept;

// 0x10023490 LoadDLCs struct fixup.
// Clears five byte fields at stride 64; then either sets byte[360]=1 or calls
// the load callback followed by the post step (sub_10023150).
void load_dlcs_struct_fixup(void* self, void (*load_callback)(void*),
                            void (*post)(void*)) noexcept;

// 0x10005d80 CollisionDeadlockFix.
// Walks the 8-byte collision records (next index at +0, flags word at +4)
// starting from the index stored at other+102 through the table at self+112.
// The blocked query receives 1 << (flags & 3) and nonzero stops the walk.
// `counter` mirrors dword_1015BDD8; the walk aborts after 10000 steps or on a
// 0xFFFF next index. Returns the final record index, or -1 on abort.
// (The original also prefetches the next record; that has no observable effect
// and is intentionally omitted.)
int collision_deadlock_walk(const void* self, const void* other, std::uint32_t* counter,
                            unsigned char (*blocked)(int)) noexcept;

// ---------------------------------------------------------------------------
// Class B - shims (pre/post work then tail call through a slot).
// ---------------------------------------------------------------------------

// 0x10005650 / 0x10005670 BenchmarkFix.
// if (!a1) return 0; a3(a2); return resume();
int benchmark_fix_shim(int a1, int a2, void (*fn)(int), int (*resume)()) noexcept;

// 0x10005af0 BuildingDynamicShadows.
// if (*(int*)object == marker_a || == marker_b) special() else default().
int building_shadow_dispatch(const void* object, int marker_a, int marker_b,
                             int (*on_default)(), int (*on_special)()) noexcept;

// 0x10006330 CopSirensFlag (eflags-preserving variant).
// Stores a1 into 0x1015936C, runs step, resumes with (a3, a2).
int cop_sirens_flag_shim(int a1, int a2, int a3, std::int32_t* stored,
                         void (*step)(), int (*resume)(int, int)) noexcept;

// 0x100064b0 DrivebyFOVFix.
int driveby_fov_shim(int a1, int a2, void (*step)(), int (*resume)(int, int)) noexcept;

// 0x10006880 CarPartsShadowFix.
int car_parts_shadow_dispatch(int object, int (*on_null)(), int (*on_present)()) noexcept;

// 0x1001b040 EpisodeSwitchFreezeFix shim.
// *(int*)(object+0x20) = value; if (value) resume(r1..r4) else 0.
int episode_switch_store_and_resume(std::int32_t* object, int value, int r1, int r2, int r3,
                                    int r4, int (*resume)(int, int, int, int)) noexcept;

// 0x1001b060 EpisodeSwitchFreezeFix shim.
int episode_switch_shim_b(int ebx, int ebp, unsigned char* edi, int (*resume)()) noexcept;

// 0x1001b0a0 EpisodeSwitchFreezeFix shim (virtual call through *(*this)+0x30).
// `vcall` receives (this[0], this[1]).
int episode_switch_vcall(const void* self, int (*vcall)(const void*, int)) noexcept;

// 0x1001b0c0 EpisodeSwitchFreezeFix shim.
int episode_switch_shim_c(const void* self, int (*resume)()) noexcept;

// 0x10036e90 GroupHackFix. if (self != 8) resume() else 0.
int group_hack_shim(const void* self, int (*resume)()) noexcept;

// 0x10036eb0 FreezeGunFix. Reads *(int*)(object+0x40); rejects 0xCDCDCDCD/-1/0.
int freeze_gun_shim(const void* object, int a2, int a3, int (*resume)(int, int)) noexcept;

// 0x10036ee0 BlackscreenFix. value = *(int*)slot; resume() unless 7 < value < 11.
int blackscreen_shim(int value, int (*resume)()) noexcept;

// 0x10036f00 CrashMsgFix (replace '~' with ' ' in place; returns the buffer).
char* crash_msg_sanitize(char* message) noexcept;

// 0x1003d7b0 PathNodeCrashFix. if (a1 == 0x1FFFFF) special() else resume().
int path_node_shim(int a1, int (*on_special)(), int (*resume)()) noexcept;

// 0x1003d870 PlaneCrashFix.
// if (!a1) return null_resume(); *(byte*)(object+0x26) = a2; return resume().
int plane_crash_shim(std::uint8_t* object, int a1, unsigned char a2,
                     int (*resume)(), int (*on_null)()) noexcept;

// 0x1003d9a0 PlayerAsPedComponentsFix.
int player_ped_components_shim(int a1, std::int32_t* stored, void (*step)(),
                               int (*resume)()) noexcept;

// 0x10044560 PopupHeadlightsFlag.
// stored = *(int16*)(object+0x2E); step(); mode_flag==1 ? resume_a : resume_b.
int popup_headlights_shim(const void* object, int a1, std::int32_t* stored,
                          const bool* mode_flag, void (*step)(),
                          int (*resume_a)(int, int), int (*resume_b)(int, int)) noexcept;

// 0x10046780 SMPA60Fix tail shim.
// v1 = *(int*)(object+0x14); if (v1 && v1 != 0xCDCDCDCD) *(int*)(v1+0x10) = *(int*)(object+0x10);
int smpa60_tail_shim(void* object, int (*resume)()) noexcept;

// 0x10046750 SMPA60Fix entry shim (marker 0xDDDDDDDD dispatch).
int smpa60_entry_shim(int ecx, const void* object, int (*on_marker)(),
                      int (*resume)(int, int, int, int, int)) noexcept;

// 0x100469e0 SuperLODFix.
// test_result mirrors sub_100469A0's return; on false the flags byte at
// object+0x72 is checked for bit 0x10: absent -> skip(), present -> resume().
int super_lod_fix_shim(void* object, bool test_result, int (*skip)(), int (*resume)()) noexcept;

// 0x1004aab0 VRAMFix.
int vram_fix_shim(int v1, int v2, std::int32_t* stored_a, std::int32_t* stored_b,
                  void (*step)(), bool override_enabled) noexcept;

// 0x10050490 WantedLevelCrashFix.
int wanted_level_shim(const void* self, void (*vcall)(const void*), int (*resume)()) noexcept;

// 0x100521e0 NewSavesCompatibility. `stack_slot` receives the spilled eax value.
int new_saves_compat_shim(int a1, int a2, int a3, std::int32_t* stack_slot, void (*pre)(),
                          int (*resume)(int, int)) noexcept;

// 0x10051600 NewFilesCompatibility (tail-call shim).
int new_files_compat_shim(int (*tail)()) noexcept;

// 0x100512f0 detour (calls sub_10050D10 then returns).
void detour_100512f0_body(void (*step)()) noexcept;

// 0x100068a0 detour (two step calls then resume).
int detour_100068a0_body(void (*step)(), int (*resume)()) noexcept;

// ---------------------------------------------------------------------------
// Class C - register-context detours.
//
// The originals save the complete register set to the stack and let a helper
// read it. The portable equivalents pass an explicit context instead.
// ---------------------------------------------------------------------------

// 0x100051b0 vehicle animation body.
struct VehicleAnimContext final {
  std::int32_t* stored_esi_1015AD38{};
  std::int32_t esi_value{};
};
void vehicle_anim_detour(VehicleAnimContext& ctx, void (*anim_step)(),
                         void (*resume)()) noexcept;

// 0x1001af60 EmissiveLerpFix.
struct EmissiveLerpContext final {
  std::int32_t* stored_ecx_10159CC0{};
  std::int32_t* stored_edx_10159CCC{};
  std::int32_t ecx_value{};
  std::int32_t edx_value{};
};
int emissive_lerp_detour(EmissiveLerpContext& ctx, void (*step)(), int (*resume)(int)) noexcept;

// 0x10037ba0 BadPlayerModelCrashFix. r1/r2 are the original's a7/a6 stack args.
struct BadPlayerModelContext final {
  std::int32_t* stored_10159C8C{};  // [ebp+0xC]
  std::int32_t* stored_10159CA4{};  // [ebp+0x10]
  std::int32_t value_c{};
  std::int32_t value_10{};
  void* object{};                    // a10 in the decompilation
};
int bad_player_model_detour(BadPlayerModelContext& ctx, void (*step)(), int r1, int r2,
                            int (*resume)(int, int)) noexcept;

// 0x1001ef60 HighFPSBikePhysicsFix.
struct BikePhysicsContext final {
  float* saved_arg_1015AD7C{};
  std::int32_t* step_result_1015AD80{};
  float arg_value{};
  int esi_value{};  // restored into ecx before the resume jump
};
int bike_physics_detour(BikePhysicsContext& ctx, int (*step)(), int esi_value,
                        int (*resume)(int, float)) noexcept;

// 0x1001f030 HighFPSBikePhysicsFix loop body. The original calls
// step(phase, arg_a, arg_b) for phase = 0..11 (verified: last call pushes 0x0B)
// after restoring the saved register context from its globals before every
// call, then jumps through slot 0x10159CFC. `phases` is the call count.
int bike_physics_loop_body(const std::int32_t* register_store, std::size_t register_count,
                           std::size_t phases, int arg_a, int arg_b,
                           int (*step)(int, int, int), int (*resume)()) noexcept;

constexpr std::size_t kBikePhysicsPhaseCount = 12;

// 0x10006310 CopSirensFlag save variant.
struct CopSirensContext final {
  std::int32_t* stored_edx_10159370{};
  std::int32_t edx_value{};
  std::int32_t loaded_eax_10159B80{};
};
int cop_sirens_detour(CopSirensContext& ctx, void (*step)(), int (*resume)(int)) noexcept;

// 0x1003d310 / 0x1003d340 / 0x1003d370 / 0x1003d3a0 NoLiveryLimit.
// All four: ArgList = object+0x13C; 0x10159BB0 = arg; sub_1003D220(); then push
// two arguments (0x10159B98 and sign-extended word at object+0x48) and resume.
// 0x1003d370 additionally pops 8 bytes before the pushes.
struct NoLiveryContext final {
  std::int32_t* arglist_10159BA8{};
  std::int32_t* stored_arg_10159BB0{};
  std::int32_t stored_10159B98{};
  void* object{};
};
int no_livery_detour(NoLiveryContext& ctx, std::int32_t stored_value,
                     std::size_t extra_stack_pop, void (*step)(),
                     int (*resume)(int, int)) noexcept;

// 0x1003d3e0 NoLiveryLimit tail body.
int no_livery_tail(NoLiveryContext& ctx, std::int32_t edi_value,
                   std::int32_t* stored_10159BA0, std::int32_t stored_10159B90,
                   void (*step_a)(), void (*step_b)(),
                   std::int32_t* object_194, int (*resume)(int, int)) noexcept;

// ---------------------------------------------------------------------------
// Class D - state-reset stubs (pusha ... zero slots ... popa; ret).
// `slots`/`count` describe the caller-owned slot pointers; each returns the
// number of slots cleared. The documented counts are the constants below.
// ---------------------------------------------------------------------------

std::size_t reset_stub_1001ec30(std::int32_t** slots, std::size_t count) noexcept;  // 3
std::size_t reset_stub_10022ed0(std::int32_t** slots, std::size_t count) noexcept;  // 5
std::size_t reset_stub_10023d70(std::int32_t** slots, std::size_t count) noexcept;  // 3
std::size_t reset_stub_1003d760(std::int32_t** slots, std::size_t count) noexcept;  // 2
std::size_t reset_stub_10044130(std::int32_t** slots, std::size_t count) noexcept;  // 3
std::size_t reset_stub_100453e0(std::int32_t** slots, std::size_t count) noexcept;  // 2
std::size_t reset_stub_10050820(std::int32_t** slots, std::size_t count) noexcept;  // 2
std::size_t reset_stub_100509e0(std::int32_t** slots, std::size_t count) noexcept;  // 2

constexpr std::size_t kResetStub1001EC30Slots = 3;
constexpr std::size_t kResetStub10022ED0Slots = 5;
constexpr std::size_t kResetStub10023D70Slots = 3;
constexpr std::size_t kResetStub1003D760Slots = 2;
constexpr std::size_t kResetStub10044130Slots = 3;
constexpr std::size_t kResetStub100453E0Slots = 2;
constexpr std::size_t kResetStub10050820Slots = 2;
constexpr std::size_t kResetStub100509E0Slots = 2;

}  // namespace zpatch::hook_bodies
