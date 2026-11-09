CXX ?= clang++
CPPFLAGS ?= -Iinclude
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
LDLIBS ?=
ifeq ($(OS),Windows_NT)
LDLIBS += -lversion
endif
SOURCES = src/config.cpp src/game.cpp src/model_catalog.cpp src/vehicle_tables.cpp src/ide_flags.cpp src/vehicle_animation.cpp src/hook_bodies.cpp src/hook_install.cpp src/skip_launcher.cpp src/skip_launcher_win.cpp src/rebased_values.cpp src/rva_installs.cpp src/op_corrections.cpp src/hook_bodies_win.cpp src/patch.cpp src/patch_catalog.cpp src/patch_manifest.cpp src/pattern.cpp src/option_flags.cpp src/recovered_sites.cpp src/recovered_ops.cpp src/recovered_extra_signatures.cpp src/recovered_rva_jumps.cpp src/recovered_sig_jumps.cpp src/recovered_indirect_writes.cpp src/recovered_match_jumps.cpp src/recovered_literal_stores.cpp src/recovered_match_literals.cpp src/recovered_build_gates.cpp src/recovered_preamble.cpp src/preamble_apply.cpp src/recovered_apply.cpp src/game_image.cpp src/menu_integration.cpp src/number_format.cpp src/zmenu_guard.cpp src/useless_checks.cpp src/runtime.cpp
BUILD = build/native

.PHONY: all test clean mingw
all: $(BUILD)/zpatch_tests $(BUILD)/zpatch_smoke
$(BUILD)/zpatch_tests: $(SOURCES) tests/test_main.cpp
	@mkdir -p $(BUILD) && true
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ $(LDLIBS) -o $@
$(BUILD)/zpatch_smoke: $(SOURCES) tests/smoke.cpp
	@mkdir -p $(BUILD) && true
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ $(LDLIBS) -o $@
test: $(BUILD)/zpatch_tests $(BUILD)/zpatch_smoke
	$(BUILD)/zpatch_tests
	$(BUILD)/zpatch_smoke
# Requires i686-w64-mingw32-g++ on PATH.
mingw:
	@mkdir -p $(BUILD) && true
	i686-w64-mingw32-g++ $(CPPFLAGS) $(CXXFLAGS) -shared $(SOURCES) src/dllmain.cpp src/ZolikaPatch.def -Wl,--exclude-all-symbols -lversion -static-libgcc -static-libstdc++ -o $(BUILD)/ZolikaPatch.asi
clean:
	rm -rf build && true && true
