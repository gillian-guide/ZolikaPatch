#include <common.hxx>

#include <climits>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <typeinfo>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

import config;
import options;

// The preserved faults in the INI loader, asserted by their *observable* behaviour
// instead of by prose. `docs/parity.md` lists them; `zp_tests` asserted the two
// conversion throws as values only (a `bool threw`), the BOM as an empty document,
// and left the null-section dereference and the 255-byte split to the comments.
// This test is the behavioural half:
//
//   1. std::invalid_argument out of a non-numeric [Options] value  -- in-process,
//      asserted by exact type (`typeid`), on `Key=abc` and on `Key=<space>`;
//   2. std::out_of_range out of a numeric value outside `int`      -- same;
//   3. a first-line UTF-8 BOM drops the whole file                -- in-process:
//      no throw, and every catalogue key keeps its arm default (the tree's model of
//      the 1.41 source; `docs/parity.md` records that the oracle's parse body has no
//      such branch, so the oracle side of this one is open);
//   4. a key line before the first [section] dereferences a null section pointer
//      -- a child process, because it kills the process: the parent asserts the
//      child's exit code and that its report stopped *after* the diagnostic line
//      that precedes the faulting parse, not before it;
//   5. a line at/over the 255-byte buffer                          -- what this tree
//      does (splits and continues) and how that differs from the original.
//
// Point 5 is where the tree and the oracle part company. The oracle's parser is
// `sub_10059D70`, feather 1.41's
//
//     char line[256];                       // FINI_BUFFER_SIZE 256
//     while (!file.eof()) { file.getline(line, 256); ... }
//
// -- `getline(&line, 0x100)` at `0x10059E0E`, the loop tail testing MSVC's eofbit
// (`test byte ptr [eax+ecx+0Ch], 1 / jz`) at `0x1005A53E`, and the same loop in
// `ZolikaPatch/INI.h`, the original's own mirror of the header. A line that fills
// the buffer sets `failbit` *without* consuming the rest, so `eof()` stays false and
// every later `getline` is a sentry no-op: the original re-reads the same buffer
// forever (a startup hang). This tree splits the line at 255 bytes and continues, so
// the test pins this tree's half and `docs/parity.md` records the difference as a
// divergence -- the original's hang is not reproduced.
//
// Nothing here needs the game or the built ASI: the loader's reader is called
// directly, exactly as `Loader::Enabled` calls it.
//
//     zp_fault_parity                    the suite
//     zp_fault_parity --child <report>   the key-before-section fault, fresh process

namespace
{
    // The original's "the process died on an access violation" status, which is
    // what a child process's exit code is when the fault is unhandled.
    constexpr DWORD kStatusAccessViolation = 0xC0000005u;

    int checks = 0;
    int failures = 0;

    void Check(bool condition, std::string_view what, std::string_view detail = {})
    {
        ++checks;
        if (condition)
        {
            std::printf("PASS %.*s\n", static_cast<int>(what.size()), what.data());
            return;
        }
        ++failures;
        std::printf("FAIL %.*s", static_cast<int>(what.size()), what.data());
        if (!detail.empty())
            std::printf(" (%s)", std::string(detail).c_str());
        std::printf("\n");
    }

    // Evidence that is worth reading on a green run too (the observed exception
    // type and message, the child's exit code, the catalogue size).
    void Detail(std::string_view text)
    {
        std::printf("     %.*s\n", static_cast<int>(text.size()), text.data());
    }

    const Option* CatalogueEntry(std::string_view key)
    {
        return Catalogue::Find(key);
    }

    // The document the loader would have parsed from a file, installed as `Config`'s
    // one global document -- the same slot `Config::Load()` fills and `Catalogue::Read`
    // reads, without touching the file system.
    void SetDocument(std::string_view text)
    {
        Config::Ini() = Feather::Parse(text);
    }

    std::string OptionText(std::string_view key, std::string_view value)
    {
        return std::string("[Options]\n") + std::string(key) + "=" + std::string(value) + "\n";
    }

    struct Raised
    {
        bool any = false;
        const std::type_info* type = nullptr;
        std::string what;
    };

    // Reads one option through the loader's own entry point (`Catalogue::Read` ->
    // `Config::Integer` -> `std::stoi`) and reports what left it. The catch is here,
    // one frame above the loader: nothing between the throw site and this call
    // catches, which is what "uncaught out of the loader" means.
    Raised ReadOption(std::string_view key)
    {
        try
        {
            (void)Catalogue::Read(key);
        }
        catch (const std::exception& error)
        {
            return Raised{ true, &typeid(error), error.what() };
        }
        catch (...)
        {
            return Raised{ true, nullptr, "<not a std::exception>" };
        }
        return Raised{};
    }

    std::string Describe(const Raised& raised)
    {
        if (!raised.any)
            return "nothing was thrown";
        return std::string("threw ") +
               (raised.type != nullptr ? raised.type->name() : "<unknown>") + " (\"" + raised.what + "\")";
    }

    // ------------------------------------------------------------------- faults 1, 2
    void CheckInvalidArgument()
    {
        const std::string key = "CarDynamicShadowFix";

        // The parse itself is quiet: `abc` is stored as text, and the loader converts
        // it only when it reads the key. The fault is the read, not the parse.
        SetDocument(OptionText(key, "abc"));
        Check(Config::Ini().Find("Options", key) != nullptr,
            "invalid_argument: the parse stores Key=abc as text");
        const auto letters = ReadOption(key);
        Check(letters.any && letters.type == &typeid(std::invalid_argument),
            "invalid_argument: Key=abc throws std::invalid_argument out of the loader",
            Describe(letters));
        Detail(Describe(letters));

        // `Key= ` -- one space. The value is not empty, so the grammar stores it, and
        // std::stoi finds no digit in it.
        SetDocument(OptionText(key, " "));
        const auto spaces = ReadOption(key);
        Check(spaces.any && spaces.type == &typeid(std::invalid_argument),
            "invalid_argument: Key=<space> also throws std::invalid_argument", Describe(spaces));
        Detail(Describe(spaces));

        // The boundary the fault does not cover: `Key=` is an empty value, which the
        // grammar does not store, so the arm's default is read and nothing throws.
        SetDocument(OptionText(key, ""));
        const auto empty = ReadOption(key);
        Check(!empty.any, "invalid_argument: Key= throws nothing (an empty value is not stored)",
            Describe(empty));
        const auto* option = CatalogueEntry(key);
        Check(option != nullptr && Catalogue::Read(key) == option->absent,
            "invalid_argument: ... Key= reads the arm's own default",
            std::format("read {}, default {}", Catalogue::Read(key),
                option != nullptr ? option->absent : 0));
    }

    void CheckOutOfRange()
    {
        const std::string key = "CarDynamicShadowFix";

        SetDocument(OptionText(key, "99999999999"));
        const auto large = ReadOption(key);
        Check(large.any && large.type == &typeid(std::out_of_range),
            "out_of_range: Key=99999999999 throws std::out_of_range out of the loader",
            Describe(large));
        Detail(Describe(large));

        SetDocument(OptionText(key, "-99999999999"));
        const auto negative = ReadOption(key);
        Check(negative.any && negative.type == &typeid(std::out_of_range),
            "out_of_range: a negative value below INT_MIN throws std::out_of_range",
            Describe(negative));
        Detail(Describe(negative));

        // The boundary is `int`, not something wider: INT_MAX and INT_MIN read, one
        // past each throws.
        SetDocument(OptionText(key, std::to_string(INT_MAX)));
        Check(Catalogue::Read(key) == INT_MAX,
            "out_of_range: INT_MAX is read, not thrown on");
        SetDocument(OptionText(key, std::to_string(INT_MIN)));
        Check(Catalogue::Read(key) == INT_MIN,
            "out_of_range: INT_MIN is read, not thrown on");
        SetDocument(OptionText(key, "2147483648"));
        const auto pastMax = ReadOption(key);
        Check(pastMax.any && pastMax.type == &typeid(std::out_of_range),
            "out_of_range: INT_MAX + 1 throws std::out_of_range", Describe(pastMax));
        SetDocument(OptionText(key, "-2147483649"));
        const auto pastMin = ReadOption(key);
        Check(pastMin.any && pastMin.type == &typeid(std::out_of_range),
            "out_of_range: INT_MIN - 1 throws std::out_of_range", Describe(pastMin));
    }

    // ----------------------------------------------------------------------- fault 3
    // The whole file, with every catalogue key set to a value that differs from its
    // arm default. It is the control: without the BOM this text changes every option.
    std::string FullOptionsText()
    {
        std::string text = "[Options]\n";
        for (const auto& option : Catalogue::All)
            text += std::string(option.key) + "=" + std::to_string(option.absent == 1 ? 0 : 1) + "\n";
        return text;
    }

    void CheckFirstLineBom()
    {
        const std::string body = FullOptionsText();

        // First line starts with 0xEF: the parse returns an empty document.
        const auto dropped = Feather::Parse("\xEF\xBB\xBF" + body);
        Check(dropped.sections.empty(), "bom: a UTF-8 BOM on the first line drops the whole file");
        Check(dropped.Section("Options") == nullptr, "bom: ... the [Options] section is not there");

        // Every option then reads its arm default, and nothing throws.
        Config::Ini() = dropped;
        int changed = 0;
        int threw = 0;
        std::string first;
        for (const auto& option : Catalogue::All)
        {
            try
            {
                const int read = Catalogue::Read(option.key);
                if (read != option.absent)
                {
                    if (changed == 0)
                        first = std::string(option.key) + "=" + std::to_string(read) + " (default " +
                                std::to_string(option.absent) + ")";
                    ++changed;
                }
            }
            catch (...)
            {
                ++threw;
            }
        }
        Check(changed == 0 && threw == 0,
            "bom: every catalogue key keeps its default after the drop",
            std::format("{} changed (first {}), {} threw", changed, first, threw));
        Detail(std::format("{} option(s) read after the drop, 0 differ, 0 threw", Catalogue::Count()));

        // The control, so the check above cannot pass vacuously: the same text
        // without the BOM sets every key, and every read answers the written value.
        Config::Ini() = Feather::Parse(body);
        int unchanged = 0;
        std::string firstUnchanged;
        for (const auto& option : Catalogue::All)
        {
            const int differ = option.absent == 1 ? 0 : 1;
            const int read = Catalogue::Read(option.key);
            if (read != differ)
            {
                if (unchanged == 0)
                    firstUnchanged = std::string(option.key) + "=" + std::to_string(read) + " (wrote " +
                                     std::to_string(differ) + ")";
                ++unchanged;
            }
        }
        Check(unchanged == 0,
            "bom: control -- without the BOM the same text sets every option",
            std::format("{} unchanged (first {})", unchanged, firstUnchanged));
    }

    // ----------------------------------------------------------------------- fault 4
    int StoredInt(const Feather::Document& document, std::string_view key, int fallback)
    {
        return Config::Integer(document, key, std::to_string(fallback));
    }

    void CheckKeyBeforeSectionInProcess()
    {
        // The fault is the store into the selected section (feather's
        // `(*current)[key] = value`). Every line shape below is a key line before
        // the first section, and each one returns before that store, so this
        // process survives: the boundary is the store, not the section pointer read.
        {
            const auto document = Feather::Parse("Key=\n[Options]\nA=1\n");
            Check(document.Find("Options", "Key") == nullptr && StoredInt(document, "A", 0) == 1,
                "key-before-section: an empty value before the section never reaches the store");
        }
        {
            const auto document = Feather::Parse("just text\n[Options]\nA=1\n");
            Check(StoredInt(document, "A", 0) == 1,
                "key-before-section: a line without '=' before the section never reaches the store");
        }
        {
            const auto document = Feather::Parse("=1\n[Options]\nA=1\n");
            Check(StoredInt(document, "A", 0) == 1,
                "key-before-section: a line starting with '=' before the section never reaches the store");
        }
        {
            const auto document = Feather::Parse("# comment\n[Options]\nA=1\n");
            Check(StoredInt(document, "A", 0) == 1,
                "key-before-section: a comment before the section is skipped harmlessly");
        }
        {
            const auto document = Feather::Parse("[Options]\nKey=1\n");
            Check(StoredInt(document, "Key", 0) == 1,
                "key-before-section: the same key line *after* a section is stored normally");
        }
    }

    std::filesystem::path SelfPath()
    {
        std::wstring buffer(MAX_PATH, L'\0');
        for (;;)
        {
            const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                static_cast<DWORD>(buffer.size()));
            if (length == 0)
                return {};
            if (length < buffer.size())
                return std::filesystem::path(std::wstring(buffer.data(), length));
            buffer.resize(buffer.size() * 2);
        }
    }

    struct ChildResult
    {
        bool ran = false;
        DWORD status = 0;
        std::vector<std::string> lines;
        std::string why;
    };

    ChildResult RunChildProcess(const std::filesystem::path& self, const std::filesystem::path& report)
    {
        ChildResult result;
        std::error_code ignored;
        std::filesystem::remove(report, ignored);

        if (self.empty())
        {
            result.why = "the test could not name its own executable";
            return result;
        }

        const std::wstring command =
            L"\"" + self.wstring() + L"\" --child \"" + report.wstring() + L"\"";
        std::vector<wchar_t> line(command.begin(), command.end());
        line.push_back(L'\0');

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                &startup, &process) == FALSE)
        {
            result.why = std::format("CreateProcess failed (error {})", GetLastError());
            return result;
        }

        WaitForSingleObject(process.hProcess, 30000);
        GetExitCodeProcess(process.hProcess, &result.status);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        result.ran = true;

        std::ifstream stream(report, std::ios::binary);
        std::string text;
        while (std::getline(stream, text))
        {
            while (!text.empty() && (text.back() == '\r' || text.back() == ' '))
                text.pop_back();
            result.lines.push_back(text);
        }
        return result;
    }

    void CheckKeyBeforeSectionChild(const std::filesystem::path& self)
    {
        // The documented preserved fault: `(*current)[key] = value` with `current`
        // null. It kills the process, so it runs in a child. The child writes one
        // line before the faulting parse (after proving, in that same process, that
        // the parser reads a normal document) and one after a *successful* parse,
        // so the parent can tell "the parse faulted" from "the child died earlier".
        const auto report = std::filesystem::current_path() / "fault_parity_child.report";
        const auto child = RunChildProcess(self, report);

        std::string joined;
        for (const auto& text : child.lines)
            joined += (joined.empty() ? "" : ", ") + text;
        if (joined.empty())
            joined = "<empty>";

        const std::vector<std::string> expected{ "PHASE=control-parsed", "PHASE=parse-start" };
        Check(child.ran && child.lines == expected,
            "key-before-section: the child wrote its diagnostic line and then never returned",
            child.ran ? "report " + joined : child.why);

        char statusText[32] = {};
        std::snprintf(statusText, sizeof(statusText), "0x%08X", static_cast<unsigned>(child.status));
        Check(child.ran && child.status == kStatusAccessViolation,
            "key-before-section: the child died on an access violation (exit 0xC0000005)",
            child.ran ? std::format("exit {}", statusText) : child.why);
        Detail(std::format("child exit {} (expected 0x{:08X})", statusText, kStatusAccessViolation));
    }

    // ----------------------------------------------------------------------- fault 5
    void CheckLongLine()
    {
        // The tree's model of feather's 256-byte buffer: `taken = min(remaining, 255)`,
        // and the bytes after the 255th are a *fresh* line. Measured at the boundary.
        {
            // "A=" + 253 = 255 bytes: the longest line that is kept whole.
            const auto document = Feather::Parse("[Options]\nA=" + std::string(253, 'x') + "\nB=1\n");
            const auto* value = document.Find("Options", "A");
            Check(value != nullptr && value->size() == 253,
                "long line: a 255-byte line is kept whole, not split",
                value != nullptr ? std::to_string(value->size()) : "absent");
            Check(StoredInt(document, "B", 0) == 1,
                "long line: ... and the line after it still parses");
        }
        {
            // One byte more does not fit: 255 bytes are taken and the rest of the line
            // starts a new one.
            const auto document = Feather::Parse("[Options]\nA=" + std::string(254, 'x') + "\nB=1\n");
            const auto* value = document.Find("Options", "A");
            Check(value != nullptr && value->size() == 253,
                "long line: a 256-byte line is cut at 255, so its value keeps 253 bytes",
                value != nullptr ? std::to_string(value->size()) : "absent");
            Check(StoredInt(document, "B", 0) == 1,
                "long line: ... and the parse continues after the cut");
        }
        {
            // The consequence the split has: the continuation is parsed as a line of
            // its own, so a key can start at byte 256 of the value's line.
            const auto document = Feather::Parse("[Options]\nA=" + std::string(253, 'x') + "B=2\n");
            const auto* value = document.Find("Options", "A");
            const auto* injected = document.Find("Options", "B");
            Check(value != nullptr && value->size() == 253 && injected != nullptr && *injected == "2",
                "long line: the bytes after the 255th are parsed as a new line (a key can start there)",
                std::format("A={}, B={}", value != nullptr ? std::to_string(value->size()) : "absent",
                    injected != nullptr ? *injected : "absent"));
        }
        {
            // A key *after* the split's remainder is still read: the parser advanced
            // past the whole long line. (This is the half of docs/parity.md's note
            // this tree can prove: it continues; the original's spin cannot be run.)
            const auto document = Feather::Parse(
                "[Options]\nA=" + std::string(300, 'x') + "\nB=1\n");
            const auto* value = document.Find("Options", "A");
            Check(value != nullptr && value->size() == 253 && StoredInt(document, "B", 0) == 1,
                "long line: the parser terminates and reads the rest of the file",
                std::format("A={}, B={}", value != nullptr ? std::to_string(value->size()) : "absent",
                    StoredInt(document, "B", 0)));
        }
    }

    // ---------------------------------------------------------------------- the child
    int RunChild(const std::filesystem::path& report)
    {
        auto append = [&report](std::string_view line)
        {
            std::ofstream stream(report, std::ios::binary | std::ios::app);
            stream << line << "\n";
        };

        // The control: this same process parses a normal document. Written first, so
        // the parent knows the report was writable and the parser ran before the
        // faulting parse.
        const auto control = Feather::Parse("[Options]\nA=1\n");
        const auto* value = control.Find("Options", "A");
        append(value != nullptr && *value == "1" ? "PHASE=control-parsed" : "PHASE=control-failed");

        // The diagnostic point: the faulting parse starts here.
        append("PHASE=parse-start");

        // A key line before the first section: the store into the null section must
        // die here.
        const auto document = Feather::Parse("Key=1\n[Options]\nA=1\n");
        append(std::format("PHASE=parse-returned sections={}", document.sections.size()));
        return 0;
    }

    int Run()
    {
        CheckInvalidArgument();
        CheckOutOfRange();
        CheckFirstLineBom();
        CheckKeyBeforeSectionInProcess();
        CheckKeyBeforeSectionChild(SelfPath());
        CheckLongLine();
        return failures == 0 ? 0 : 1;
    }
}

int main(int argc, char** argv)
{
    // This test kills a child process on purpose (and would kill its own process if
    // the child's case were ever run here), so a fault must be a result, never a
    // Windows "Application Error" dialog on whoever runs the suite. The error mode
    // is inherited by the child, and the child calls this line too.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    if (argc >= 2 && std::strcmp(argv[1], "--child") == 0)
        return RunChild(argc >= 3 ? std::filesystem::path(argv[2]) : std::filesystem::path());

    try
    {
        Run();
    }
    catch (const std::exception& error)
    {
        Check(false, "the test itself threw", error.what());
    }

    std::printf("%d check(s), %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
