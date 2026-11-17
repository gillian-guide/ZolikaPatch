module;

#include <common.hxx>

export module patch;

import common;
import game;

// Signature scanning and the recovered-signature normalisation. Every write goes
// through injector directly (MakeNOP, MakeCALL, MakeJMP, MakeRET, WriteMemory);
// nothing here re-implements the library.

// A recovered signature is written the way IDA prints it: `??` is one wildcard
// byte. The pinned Hooking.Patterns 460a47b parser consumes one `?` per wildcard
// byte (`TransformPattern` in Hooking.Patterns.cpp), so an IDA-style `??` token
// would otherwise widen the pattern by a byte. Recovered strings are therefore
// normalised here, once, at the boundary -- the feature modules keep the recovered
// text verbatim. The hex digits are uppercased and the whitespace collapsed at the
// same time, so the string the scanner caches per signature is canonical.
export std::string NormaliseSignature(std::string_view recovered)
{
    std::string result;
    size_t offset = 0;
    while (offset < recovered.size())
    {
        while (offset < recovered.size() && std::isspace(static_cast<unsigned char>(recovered[offset])))
            ++offset;

        const auto start = offset;
        while (offset < recovered.size() && !std::isspace(static_cast<unsigned char>(recovered[offset])))
            ++offset;
        if (start == offset)
            break;

        const auto token = recovered.substr(start, offset - start);
        if (!result.empty())
            result.push_back(' ');

        if (token.find_first_not_of('?') == std::string_view::npos)
        {
            result.push_back('?');
            continue;
        }
        for (const auto character : token)
            result.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(character))));
    }
    return result;
}

// Tries each candidate signature in order and returns the first one that matches
// at least once. A one-string call is a single signature, a two-string call is a
// build variant, a three-string call is three builds. `count` is a scan hint, not
// a minimum: the scan stops after that many hits and returns what it found.
//
// The module-base constructor makes Hooking.Patterns compute the range itself,
// from the image base to the end of the last executable section. The offline
// tools load a copy of the image instead of the image itself, so passing the copy's
// base makes their scans cover the same bytes the game's module would.
//
// The scanner caches hits per signature string for the life of the process: a
// second scan of the same string answers from that cache whatever `count` asks
// for. A signature that has to be scanned with two different counts is two
// signatures.
export template <size_t count = 1, typename... Args>
hook::pattern find_pattern(Args... args)
{
    const auto base = reinterpret_cast<void*>(Game::Get().ImageBase());
    hook::pattern pattern;
    ((pattern = hook::pattern(base, NormaliseSignature(args)), !pattern.count_hint(count).empty()) || ...);
    return pattern;
}
