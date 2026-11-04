module;

#include <common.hxx>

export module config;

import common;

// The INI boundary. The original reads ZolikaPatch.ini through feather-ini-parser
// 1.41, a third-party library it links statically; its grammar is the contract,
// quirks and all (tests/config_contract pins every one of them):
//
//   * a 255-character line buffer: a longer line is read as two lines;
//   * a first line starting with 0xEF (a UTF-8 BOM) aborts the parse -- the whole
//     file is dropped and every option keeps its default;
//   * only `#` and `//` start a comment, and only at the start of a line;
//   * a section line is `[...]`, and re-opening a section starts it empty;
//   * a key line is split on '=' with strtok semantics, the key keeps any
//     trailing space (`Key = 1` is the key `Key `, which nothing asks for), and
//     the value keeps its leading space;
//   * an empty value is not stored, so `Key=` falls back to the default;
//   * a repeated key inside one section keeps the last value;
//   * a key before the first section dereferences a null section pointer -- the
//     original's fault, preserved here.
export namespace Feather
{
    using Keys = std::map<std::string, std::string>;

    struct Document
    {
        std::map<std::string, Keys> sections;

        const Keys* Section(std::string_view name) const
        {
            const auto found = sections.find(std::string(name));
            return found == sections.end() ? nullptr : &found->second;
        }

        const std::string* Find(std::string_view section, std::string_view key) const
        {
            const auto* keys = Section(section);
            if (keys == nullptr)
                return nullptr;
            const auto found = keys->find(std::string(key));
            return found == keys->end() ? nullptr : &found->second;
        }
    };

    inline Document Parse(std::string_view text)
    {
        Document document;
        Keys* current = nullptr;
        size_t offset = 0;
        bool first = true;

        while (offset < text.size())
        {
            const auto newline = text.find('\n', offset);
            const auto remaining = (newline == std::string_view::npos ? text.size() : newline) - offset;
            const auto taken = std::min<size_t>(remaining, 255);
            std::string line(text.substr(offset, taken));
            offset += taken;
            if (taken == remaining && offset < text.size() && text[offset] == '\n')
                ++offset;

            if (first)
            {
                first = false;
                if (!line.empty() && static_cast<unsigned char>(line[0]) == 0xEF)
                    return Document{};
            }

            if (line.empty())
                continue;
            if ((line.size() >= 2 && line[0] == '/' && line[1] == '/') || line[0] == '#')
                continue;

            if (line[0] == '[')
            {
                size_t length = line.size() >= 2 ? line.size() - 2 : 0;
                while (length + 1 < line.size() && std::isspace(static_cast<unsigned char>(line[length + 1])))
                    --length;

                current = &document.sections[line.substr(1, length)];
                current->clear();
                continue;
            }

            const auto start = line.find_first_not_of('=');
            if (start == std::string::npos)
                continue;
            const auto keyEnd = line.find('=', start);
            if (keyEnd == std::string::npos)
                continue;

            std::string key = line.substr(start, keyEnd - start);
            const auto firstNonSpace = key.find_first_not_of(" \t\n\v\f\r");
            key.erase(0, firstNonSpace == std::string::npos ? key.size() : firstNonSpace);

            const auto value = line.substr(keyEnd + 1);
            if (value.empty())
                continue;

            // The original dereferences the selected section pointer here; a key
            // line before the first section line faults it.
            (*current)[key] = value;
        }

        return document;
    }

    // The file is opened by name exactly as the original asks for it. A missing
    // file is an empty document: every option keeps its default.
    inline Document Load(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return Document{};

        std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        // The original reads in text mode, so the CRT folds CRLF to LF before the
        // parser sees it.
        std::string normalised;
        normalised.reserve(text.size());
        for (size_t index = 0; index < text.size(); ++index)
        {
            if (text[index] == '\r' && index + 1 < text.size() && text[index + 1] == '\n')
                continue;
            normalised.push_back(text[index]);
        }

        return Parse(normalised);
    }

    // The value the original would hand to std::stoi: the stored string, or the
    // caller's default text when the key is absent or empty.
    inline const std::string& Value(const Document& document, std::string_view section, std::string_view key, const std::string& fallback)
    {
        const auto* found = document.Find(section, key);
        return found != nullptr ? *found : fallback;
    }
}

// What a feature reads: the original's `std::stoi(ini.get(key, default))`, with
// the same conversion (and the same std::invalid_argument / std::out_of_range it
// throws, uncaught, on a non-numeric value).
export namespace Config
{
    // The original's file name, opened relative to the process working directory
    // exactly as it is written here.
    inline constexpr std::string_view FileName = "ZolikaPatch.ini";

    inline Feather::Document& Ini()
    {
        static Feather::Document document;
        return document;
    }

    // Reads the file once. A missing file leaves the document empty, which is the
    // original's behaviour: every option then keeps its default.
    inline bool Load()
    {
        std::error_code ignored;
        const bool exists = std::filesystem::exists(std::filesystem::path(FileName), ignored);
        Ini() = Feather::Load(std::filesystem::path(FileName));
        return exists;
    }

    inline int32_t Integer(const Feather::Document& document, std::string_view key, std::string_view fallback)
    {
        const std::string defaultText(fallback);
        return std::stoi(Feather::Value(document, "Options", key, defaultText));
    }

    inline int32_t Integer(const Feather::Document& document, std::string_view section, std::string_view key, std::string_view fallback)
    {
        const std::string defaultText(fallback);
        return std::stoi(Feather::Value(document, section, key, defaultText));
    }

    inline float Float(const Feather::Document& document, std::string_view section, std::string_view key, std::string_view fallback)
    {
        const std::string defaultText(fallback);
        return std::stof(Feather::Value(document, section, key, defaultText));
    }
}
