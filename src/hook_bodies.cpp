// Portable reimplementations of the recovered ZolikaPatch 7.65 hook bodies.
// See include/zpatch/hook_bodies.hpp for provenance notes. Every function here
// mirrors the behaviour of the original body at the documented reference VA;
// game interactions go through explicit call slots / parameters so the logic
// is unit-testable off the game.

#include "zpatch/hook_bodies.hpp"

#include <cstring>

namespace zpatch::hook_bodies {
namespace {

constexpr std::int32_t kSentinelCDCDCDCD = static_cast<std::int32_t>(0xCDCDCDCDu);
constexpr int kPathNodeSpecialValue = 0x1FFFFF;         // 2097119
constexpr int kSmpa60MarkerValue = static_cast<int>(0xDDDDDDDDu);
constexpr int kSmpa60InvalidField = -842150451;         // 0xCDCDCDCD

}  // namespace

// ---------------------------------------------------------------------------
// Class A
// ---------------------------------------------------------------------------

int aim_projectile_crash_fix(void* self, int a2, int a3, int a4) noexcept {
  if (!self) return 0;  // original dereferences unguarded; caller checks first
  auto* fields = static_cast<std::int32_t*>(self);
  fields[8] = a3;
  fields[9] = a4;
  const int result = a4 != 0 ? a2 : 0;
  fields[7] = result;
  return result;
}

int better_mp_sync() noexcept { return 0; }

int combined_radio_freeze_fix() noexcept { return 19; }

int load_dlcs_dummy() noexcept { return 1; }

int rgsc_login_removed() noexcept { return 1; }

int savegame_fix_null() noexcept { return 0; }

int savegame_fix_close(void* handle, void (*close_handle)(void*)) noexcept {
  if (close_handle && handle) close_handle(handle);
  return 0;
}

int savegame_fix_mutex(void* out_handle, void* (*create_mutex)()) noexcept {
  if (!out_handle) return 1;  // original: `if (!a2) return 1;`
  *static_cast<void**>(out_handle) = create_mutex ? create_mutex() : nullptr;
  return 0;
}

BorderlessStyles borderless_style_fixup(std::int32_t style, std::int32_t ex_style) noexcept {
  BorderlessStyles out;
  out.style = style & static_cast<std::int32_t>(0xFF30FFFFu);
  out.ex_style = ex_style & static_cast<std::int32_t>(0xFFFFFFF7u);  // clear WS_EX_TOPMOST
  return out;
}

void* improved_shader_streaming_find(void* const* entries, std::uint16_t count, int query,
                                     ShaderStreamingNameFn get_name,
                                     ShaderStreamingFilterFn filter) noexcept {
  if (!entries || count == 0) return nullptr;
  std::size_t index = 0;
  for (;;) {
    void* entry = entries[index];
    if (entry) {
      const void* name = get_name ? get_name(entry) : nullptr;
      // Original: `if (!sub_100459E0(...)) break;` - the filter returns 0 when
      // the entry matches, and that entry is what gets returned.
      if (filter && filter(query, name) == 0) break;
    }
    if (++index >= count) return nullptr;
  }
  return entries[index];
}

SpeedupTimingConstants speedup_timing_constants(std::int64_t qpc_frequency) noexcept {
  SpeedupTimingConstants out;
  if (qpc_frequency <= 0) return out;
  const double seconds_per_tick = 1000.0 / static_cast<double>(qpc_frequency);
  out.seconds_per_tick = static_cast<float>(seconds_per_tick);
  out.per_tick_ms = static_cast<float>(seconds_per_tick * 1000.0);
  out.ticks_per_second = static_cast<float>(0.001000000047497451 / seconds_per_tick);
  out.per_tick_seconds = static_cast<float>(seconds_per_tick * 0.001000000047497451);
  return out;
}

bool ped_models_in_cutscenes_check(const void* object, unsigned int (*hash)(const char*),
                                   void (*lookup)(unsigned int, int*)) noexcept {
  if (!object || !hash || !lookup) return false;
  const unsigned int player_hash = hash("player");
  int player_index = -1;
  lookup(player_hash, &player_index);
  const auto model = *reinterpret_cast<const std::int16_t*>(
      static_cast<const std::uint8_t*>(object) + 0x2E);
  return model == player_index;
}

bool new_files_compat_update(const char* name_field, void* insert_target,
                             GameCallSlots& slots) noexcept {
  if (!name_field || !*name_field) return false;
  void* container = slots.dictionary_container ? slots.dictionary_container() : nullptr;
  const unsigned int hash =
      slots.hash_name ? slots.hash_name(name_field) : 0u;
  if (slots.dictionary_contains && container && slots.dictionary_contains(container, hash)) {
    if (slots.dictionary_insert && insert_target)
      slots.dictionary_insert(insert_target, name_field);
  }
  return true;
}

int episode_switch_sum(const void* self) noexcept {
  if (!self) return 0;
  const auto* words = static_cast<const std::int32_t*>(self);
  return words[3] + words[4];
}

void* new_memory_system_apply(void* allocator, void* vtable) noexcept {
  if (allocator) *static_cast<void**>(allocator) = vtable;
  return allocator;
}

void load_dlcs_struct_fixup(void* self, void (*load_callback)(void*),
                            void (*post)(void*)) noexcept {
  if (!self) return;
  auto* bytes = static_cast<std::uint8_t*>(self);
  bytes[0] = 0;
  bytes[64] = 0;
  bytes[128] = 0;
  bytes[192] = 0;
  bytes[256] = 0;
  if (bytes[359]) {
    bytes[360] = 1;
  } else {
    if (load_callback) load_callback(self);
    if (post) post(self);
    bytes[0xA4] = 1;  // verified at 0x100234d1: mov byte ptr [esi+0A4h], 1
  }
}

int collision_deadlock_walk(const void* self, const void* other, std::uint32_t* counter,
                            unsigned char (*blocked)(int)) noexcept {
  if (counter) *counter = 0;
  if (!self || !other || !blocked) return -1;
  const auto* self_bytes = static_cast<const std::uint8_t*>(self);
  const auto* other_bytes = static_cast<const std::uint8_t*>(other);
  const auto* table = *reinterpret_cast<const std::uint8_t* const*>(self_bytes + 112);
  if (!table) return -1;
  std::uint16_t index = *reinterpret_cast<const std::uint16_t*>(other_bytes + 102);
  if (index == 0xFFFF) return -1;
  std::uint32_t steps = 0;
  for (;;) {
    const std::uint8_t* record = table + 8u * index;
    const std::uint16_t next = *reinterpret_cast<const std::uint16_t*>(record);
    // Original prefetches the next record here; not observable, omitted.
    const std::uint16_t flags = *reinterpret_cast<const std::uint16_t*>(record + 4);
    if (blocked(1 << (flags & 3))) break;
    index = next;
    ++steps;
    if (counter) *counter = steps;
    if (steps == 10000 || index == 0xFFFF) return -1;
  }
  return index;
}

// ---------------------------------------------------------------------------
// Class B
// ---------------------------------------------------------------------------

int benchmark_fix_shim(int a1, int a2, void (*fn)(int), int (*resume)()) noexcept {
  if (!a1) return 0;
  if (fn) fn(a2);
  return resume ? resume() : 0;
}

int building_shadow_dispatch(const void* object, int marker_a, int marker_b,
                             int (*on_default)(), int (*on_special)()) noexcept {
  const int value = object ? *static_cast<const std::int32_t*>(object) : 0;
  if (value == marker_a || value == marker_b)
    return on_special ? on_special() : 0;
  return on_default ? on_default() : 0;
}

int cop_sirens_flag_shim(int a1, int a2, int a3, std::int32_t* stored, void (*step)(),
                         int (*resume)(int, int)) noexcept {
  if (stored) *stored = a1;
  if (step) step();  // original wraps this in pushf/popf; flags are not observable here
  return resume ? resume(a3, a2) : 0;
}

int driveby_fov_shim(int a1, int a2, void (*step)(), int (*resume)(int, int)) noexcept {
  if (step) step();
  return resume ? resume(a1, a2) : 0;
}

int car_parts_shadow_dispatch(int object, int (*on_null)(), int (*on_present)()) noexcept {
  if (object) return on_present ? on_present() : 0;
  return on_null ? on_null() : 0;
}

int episode_switch_store_and_resume(std::int32_t* object, int value, int r1, int r2, int r3,
                                    int r4, int (*resume)(int, int, int, int)) noexcept {
  if (object) object[8] = value;  // object + 0x20
  if (value == 0) return 0;
  return resume ? resume(r1, r2, r3, r4) : 0;
}

int episode_switch_shim_b(int ebx, int ebp, unsigned char* edi, int (*resume)()) noexcept {
  if (ebx) return resume ? resume() : 0;
  if (edi) *edi = 0;
  return ebp;
}

int episode_switch_vcall(const void* self, int (*vcall)(const void*, int)) noexcept {
  if (!self) return 0;
  const auto* words = static_cast<const std::int32_t*>(self);
  const auto* object = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(words[0]));
  if (!object) return 0;
  return vcall ? vcall(object, words[1]) : 0;
}

int episode_switch_shim_c(const void* self, int (*resume)()) noexcept {
  if (!self) return 0;
  return resume ? resume() : 0;
}

int group_hack_shim(const void* self, int (*resume)()) noexcept {
  if (self != reinterpret_cast<const void*>(static_cast<std::uintptr_t>(8)))
    return resume ? resume() : 0;
  return 0;
}

int freeze_gun_shim(const void* object, int a2, int a3, int (*resume)(int, int)) noexcept {
  if (!object) return 0;
  const auto value = *reinterpret_cast<const std::int32_t*>(
      static_cast<const std::uint8_t*>(object) + 0x40);
  if (value == kSentinelCDCDCDCD || value == -1 || value == 0) return 0;
  return resume ? resume(a2, a3) : 0;
}

int blackscreen_shim(int value, int (*resume)()) noexcept {
  if (value <= 7 || value >= 11) return resume ? resume() : 0;
  return value;
}

char* crash_msg_sanitize(char* message) noexcept {
  if (!message) return message;
  for (char* cursor = message; *cursor; ++cursor) {
    if (*cursor == '~') *cursor = ' ';
  }
  return message;
}

int path_node_shim(int a1, int (*on_special)(), int (*resume)()) noexcept {
  if (a1 == kPathNodeSpecialValue) return on_special ? on_special() : 0;
  return resume ? resume() : 0;
}

int plane_crash_shim(std::uint8_t* object, int a1, unsigned char a2, int (*resume)(),
                     int (*on_null)()) noexcept {
  if (!a1) return on_null ? on_null() : 0;
  if (object) object[0x26] = a2;
  return resume ? resume() : 0;
}

int player_ped_components_shim(int a1, std::int32_t* stored, void (*step)(),
                               int (*resume)()) noexcept {
  if (stored) *stored = a1;
  if (step) step();
  return resume ? resume() : 0;
}

int popup_headlights_shim(const void* object, int a1, std::int32_t* stored,
                          const bool* mode_flag, void (*step)(),
                          int (*resume_a)(int, int), int (*resume_b)(int, int)) noexcept {
  const int value = object ? *reinterpret_cast<const std::int16_t*>(
                                 static_cast<const std::uint8_t*>(object) + 0x2E)
                           : 0;
  if (stored) *stored = value;
  if (step) step();
  const bool mode_a = mode_flag && *mode_flag;
  if (mode_a) return resume_a ? resume_a(a1, value) : 0;
  return resume_b ? resume_b(a1, value) : 0;
}

int smpa60_tail_shim(void* object, int (*resume)()) noexcept {
  if (object) {
    auto* words = static_cast<std::int32_t*>(object);
    const std::int32_t v1 = words[5];  // object + 0x14
    if (v1 && v1 != kSmpa60InvalidField) {
      *reinterpret_cast<std::int32_t*>(static_cast<std::uintptr_t>(v1) + 16) = words[4];
    }
  }
  return resume ? resume() : 0;
}

int smpa60_entry_shim(int ecx, const void* object, int (*on_marker)(),
                      int (*resume)(int, int, int, int, int)) noexcept {
  if (ecx == kSmpa60MarkerValue) return on_marker ? on_marker() : 0;
  if (!object) return 0;
  const auto* bytes = static_cast<const std::uint8_t*>(object);
  const int field = *reinterpret_cast<const std::int32_t*>(bytes + 0x70);
  const int object_40 = static_cast<int>(
      reinterpret_cast<std::uintptr_t>(bytes + 0x40));
  const int field_plus_10 = field + 0x10;
  // cdecl push order (last push is first argument): field, field,
  // field+0x10, field, object+0x40.
  return resume ? resume(field, field, field_plus_10, field, object_40) : 0;
}

int super_lod_fix_shim(void* object, bool test_result, int (*skip)(),
                       int (*resume)()) noexcept {
  if (test_result) return resume ? resume() : 0;
  const bool flag_set =
      object && (*reinterpret_cast<const std::uint8_t*>(
                     static_cast<const std::uint8_t*>(object) + 0x72) &
                 0x10) != 0;
  if (!flag_set) return skip ? skip() : 0;
  return resume ? resume() : 0;
}

int vram_fix_shim(int v1, int v2, std::int32_t* stored_a, std::int32_t* stored_b,
                  void (*step)(), bool override_enabled) noexcept {
  if (stored_a) *stored_a = v2;  // 0x1015D630 receives the [esp-8h] value
  if (stored_b) *stored_b = v1;  // 0x1015D634 receives the [esp-Ch] value
  if (step) step();
  if (override_enabled) return 0x7FFFFFFF;
  return v1;
}

int wanted_level_shim(const void* self, void (*vcall)(const void*),
                      int (*resume)()) noexcept {
  if (!self) return 0;
  if (vcall) vcall(self);
  return resume ? resume() : 0;
}

int new_saves_compat_shim(int a1, int a2, int a3, std::int32_t* stack_slot, void (*pre)(),
                          int (*resume)(int, int)) noexcept {
  if (pre) pre();
  if (stack_slot) *stack_slot = a1;  // the original spills eax into STACK[0x1E0]
  return resume ? resume(a3, a2) : 0;
}

int new_files_compat_shim(int (*tail)()) noexcept { return tail ? tail() : 0; }

void detour_100512f0_body(void (*step)()) noexcept {
  if (step) step();
}

int detour_100068a0_body(void (*step)(), int (*resume)()) noexcept {
  if (step) {
    step();
    step();
  }
  return resume ? resume() : 0;
}

// ---------------------------------------------------------------------------
// Class C
// ---------------------------------------------------------------------------

void vehicle_anim_detour(VehicleAnimContext& ctx, void (*anim_step)(),
                         void (*resume)()) noexcept {
  if (ctx.stored_esi_1015AD38) *ctx.stored_esi_1015AD38 = ctx.esi_value;
  if (anim_step) anim_step();
  if (resume) resume();
}

int emissive_lerp_detour(EmissiveLerpContext& ctx, void (*step)(),
                         int (*resume)(int)) noexcept {
  if (ctx.stored_ecx_10159CC0) *ctx.stored_ecx_10159CC0 = ctx.ecx_value;
  if (ctx.stored_edx_10159CCC) *ctx.stored_edx_10159CCC = ctx.edx_value;
  if (step) step();
  const int ecx = ctx.stored_ecx_10159CC0 ? *ctx.stored_ecx_10159CC0 : ctx.ecx_value;
  return resume ? resume(ecx) : 0;
}

int bad_player_model_detour(BadPlayerModelContext& ctx, void (*step)(), int r1, int r2,
                            int (*resume)(int, int)) noexcept {
  if (ctx.stored_10159C8C) *ctx.stored_10159C8C = ctx.value_c;
  if (ctx.stored_10159CA4) *ctx.stored_10159CA4 = ctx.value_10;
  if (step) step();
  if (ctx.object && ctx.stored_10159CA4) {
    *reinterpret_cast<std::int32_t*>(static_cast<std::uint8_t*>(ctx.object) + 16) =
        *ctx.stored_10159CA4;
  }
  return resume ? resume(r1, r2) : 0;
}

int bike_physics_detour(BikePhysicsContext& ctx, int (*step)(), int esi_value,
                        int (*resume)(int, float)) noexcept {
  if (ctx.saved_arg_1015AD7C) *ctx.saved_arg_1015AD7C = ctx.arg_value;
  const int step_result = step ? step() : 0;
  if (ctx.step_result_1015AD80) *ctx.step_result_1015AD80 = step_result;
  return resume ? resume(esi_value, ctx.arg_value) : 0;
}

int bike_physics_loop_body(const std::int32_t* register_store, std::size_t register_count,
                           std::size_t phases, int arg_a, int arg_b,
                           int (*step)(int, int, int), int (*resume)()) noexcept {
  if (!step) return -1;
  std::int32_t context[32]{};
  const std::size_t copy_count =
      register_count < (sizeof(context) / sizeof(context[0]))
          ? register_count
          : (sizeof(context) / sizeof(context[0]));
  for (std::size_t phase = 0; phase < phases; ++phase) {
    if (register_store) std::memcpy(context, register_store, copy_count * sizeof(std::int32_t));
    step(static_cast<int>(phase), arg_a, arg_b);
  }
  return resume ? resume() : 0;
}

int cop_sirens_detour(CopSirensContext& ctx, void (*step)(), int (*resume)(int)) noexcept {
  if (ctx.stored_edx_10159370) *ctx.stored_edx_10159370 = ctx.edx_value;
  if (step) step();
  return resume ? resume(ctx.loaded_eax_10159B80) : 0;
}

int no_livery_detour(NoLiveryContext& ctx, std::int32_t stored_value,
                     std::size_t extra_stack_pop, void (*step)(),
                     int (*resume)(int, int)) noexcept {
  (void)extra_stack_pop;  // 0x1003d370 only pops 8 bytes before the pushes
  if (ctx.arglist_10159BA8 && ctx.object) {
    *ctx.arglist_10159BA8 = static_cast<std::int32_t>(
        reinterpret_cast<std::uintptr_t>(static_cast<std::uint8_t*>(ctx.object) + 0x13C));
  }
  if (ctx.stored_arg_10159BB0) *ctx.stored_arg_10159BB0 = stored_value;
  if (step) step();
  const int word_48 = ctx.object
                          ? *reinterpret_cast<const std::int16_t*>(
                                static_cast<const std::uint8_t*>(ctx.object) + 0x48)
                          : 0;
  return resume ? resume(word_48, ctx.stored_10159B98) : 0;
}

int no_livery_tail(NoLiveryContext& ctx, std::int32_t edi_value,
                   std::int32_t* stored_10159BA0, std::int32_t stored_10159B90,
                   void (*step_a)(), void (*step_b)(), std::int32_t* object_194,
                   int (*resume)(int, int)) noexcept {
  if (ctx.stored_arg_10159BB0) *ctx.stored_arg_10159BB0 = edi_value;
  if (ctx.arglist_10159BA8 && ctx.object) {
    *ctx.arglist_10159BA8 = static_cast<std::int32_t>(
        reinterpret_cast<std::uintptr_t>(static_cast<std::uint8_t*>(ctx.object) + 0x13C));
  }
  if (stored_10159BA0) *stored_10159BA0 = 0;
  if (step_a) step_a();
  if (step_b) step_b();
  if (object_194) *object_194 = stored_10159B90;
  return resume ? resume(ctx.stored_10159B98, edi_value) : 0;
}

// ---------------------------------------------------------------------------
// Class D
// ---------------------------------------------------------------------------

namespace {

std::size_t clear_slots(std::int32_t** slots, std::size_t count) noexcept {
  if (!slots) return 0;
  std::size_t cleared = 0;
  for (std::size_t i = 0; i < count; ++i) {
    if (!slots[i]) continue;
    *slots[i] = 0;
    ++cleared;
  }
  return cleared;
}

}  // namespace

std::size_t reset_stub_1001ec30(std::int32_t** slots, std::size_t count) noexcept {
  return clear_slots(slots, count < kResetStub1001EC30Slots ? count : kResetStub1001EC30Slots);
}

std::size_t reset_stub_10022ed0(std::int32_t** slots, std::size_t count) noexcept {
  return clear_slots(slots, count < kResetStub10022ED0Slots ? count : kResetStub10022ED0Slots);
}

std::size_t reset_stub_10023d70(std::int32_t** slots, std::size_t count) noexcept {
  return clear_slots(slots, count < kResetStub10023D70Slots ? count : kResetStub10023D70Slots);
}

std::size_t reset_stub_1003d760(std::int32_t** slots, std::size_t count) noexcept {
  return clear_slots(slots, count < kResetStub1003D760Slots ? count : kResetStub1003D760Slots);
}

std::size_t reset_stub_10044130(std::int32_t** slots, std::size_t count) noexcept {
  return clear_slots(slots, count < kResetStub10044130Slots ? count : kResetStub10044130Slots);
}

std::size_t reset_stub_100453e0(std::int32_t** slots, std::size_t count) noexcept {
  return clear_slots(slots, count < kResetStub100453E0Slots ? count : kResetStub100453E0Slots);
}

std::size_t reset_stub_10050820(std::int32_t** slots, std::size_t count) noexcept {
  return clear_slots(slots, count < kResetStub10050820Slots ? count : kResetStub10050820Slots);
}

std::size_t reset_stub_100509e0(std::int32_t** slots, std::size_t count) noexcept {
  return clear_slots(slots, count < kResetStub100509E0Slots ? count : kResetStub100509E0Slots);
}

}  // namespace zpatch::hook_bodies
