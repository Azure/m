// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

// M4-6 (windows-platform-isolation): pin Rust<->C++ parity of the ordinal sort
// key and case-insensitive comparator against the shared golden fixture
//   crates/windows-text/testdata/ordinal_golden_vectors.txt   (D8 / D12).
//
// Windows-only. The fixture is the *ordinal* (Win32) definition, so it is
// reproduced here with the same OS primitives the Rust generator used:
//   * the sort key is LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE)
//     serialized as big-endian u16 code units (matches windows-text-sys);
//   * the comparator is the production m::case_insensitive_less, which on
//     Windows is CompareStringOrdinal(bIgnoreCase = TRUE).
//
// Note on the sort key: the C++ PIL stack does not *materialize* a byte sort
// key in production (its case-insensitive maps key on m::case_insensitive_less
// directly). The per-`I`-row assertion below therefore pins the documented
// sort-key *format* cross-language by reconstructing it from the same
// documented algorithm, demonstrating C++ reproduces the exact Rust bytes.

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#undef NOMINMAX
#define NOMINMAX
#include <Windows.h>

#include <gtest/gtest.h>

#include <m/strings/compare.h>

namespace
{
    int
    hex_nibble(char c)
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    }

    // <input-hex>: big-endian u16 code units as lowercase hex, or "-" if empty.
    std::wstring
    parse_input_hex(std::string_view hex)
    {
        std::wstring result;
        if (hex == "-")
            return result;
        EXPECT_EQ(hex.size() % 4, 0u) << "input-hex must be a whole number of u16 units";
        for (size_t i = 0; i + 4 <= hex.size(); i += 4)
        {
            uint16_t unit = 0;
            for (size_t j = 0; j < 4; ++j)
            {
                int const nyb = hex_nibble(hex[i + j]);
                EXPECT_GE(nyb, 0) << "bad hex digit in input-hex";
                unit = static_cast<uint16_t>((unit << 4) | static_cast<uint16_t>(nyb));
            }
            result.push_back(static_cast<wchar_t>(unit));
        }
        return result;
    }

    // <sortkey-hex>: raw key bytes as lowercase hex, or "-" if empty.
    std::vector<uint8_t>
    parse_bytes_hex(std::string_view hex)
    {
        std::vector<uint8_t> result;
        if (hex == "-")
            return result;
        EXPECT_EQ(hex.size() % 2, 0u) << "sortkey-hex must be a whole number of bytes";
        for (size_t i = 0; i + 2 <= hex.size(); i += 2)
        {
            int const hi = hex_nibble(hex[i]);
            int const lo = hex_nibble(hex[i + 1]);
            EXPECT_GE(hi, 0);
            EXPECT_GE(lo, 0);
            result.push_back(static_cast<uint8_t>((hi << 4) | lo));
        }
        return result;
    }

    // Reproduce the documented sort key: uppercase via LCMapStringEx, then
    // serialize each u16 code unit big-endian (high byte first).
    std::vector<uint8_t>
    compute_sort_key(std::wstring const& input)
    {
        std::vector<uint8_t> bytes;
        if (input.empty())
            return bytes;

        int const needed = ::LCMapStringEx(LOCALE_NAME_INVARIANT,
                                           LCMAP_UPPERCASE,
                                           input.data(),
                                           static_cast<int>(input.size()),
                                           nullptr,
                                           0,
                                           nullptr,
                                           nullptr,
                                           0);
        EXPECT_GT(needed, 0) << "LCMapStringEx sizing failed";

        std::wstring upper(static_cast<size_t>(needed), L'\0');
        int const written = ::LCMapStringEx(LOCALE_NAME_INVARIANT,
                                            LCMAP_UPPERCASE,
                                            input.data(),
                                            static_cast<int>(input.size()),
                                            upper.data(),
                                            needed,
                                            nullptr,
                                            nullptr,
                                            0);
        EXPECT_EQ(written, needed) << "LCMapStringEx mapping failed";
        upper.resize(static_cast<size_t>(written));

        bytes.reserve(upper.size() * 2);
        for (wchar_t const wc : upper)
        {
            uint16_t const u = static_cast<uint16_t>(wc);
            bytes.push_back(static_cast<uint8_t>(u >> 8));
            bytes.push_back(static_cast<uint8_t>(u & 0xff));
        }
        return bytes;
    }

    struct golden_vectors
    {
        std::vector<std::wstring>          corpus; // index -> input code units
        std::vector<std::vector<uint8_t>>  keys;   // index -> sort-key bytes
        // (i, j, sign): sign is -1 (lt), 0 (eq), +1 (gt) for compare(i, j).
        std::vector<std::tuple<size_t, size_t, int>> compares;
    };

    golden_vectors
    load_golden_vectors()
    {
        golden_vectors gv;
        std::ifstream in(M_ORDINAL_GOLDEN_VECTORS, std::ios::binary);
        EXPECT_TRUE(in.is_open()) << "cannot open fixture: " << M_ORDINAL_GOLDEN_VECTORS;

        std::string line;
        while (std::getline(in, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty() || line[0] == '#')
                continue;

            std::istringstream ls(line);
            std::string kind;
            ls >> kind;

            if (kind == "V")
            {
                int version = 0;
                ls >> version;
                EXPECT_EQ(version, 1) << "unexpected fixture version";
            }
            else if (kind == "I")
            {
                size_t      index = 0;
                std::string input_hex;
                std::string sortkey_hex;
                ls >> index >> input_hex >> sortkey_hex;
                EXPECT_EQ(index, gv.corpus.size()) << "I rows must be index-ordered";
                gv.corpus.push_back(parse_input_hex(input_hex));
                gv.keys.push_back(parse_bytes_hex(sortkey_hex));
            }
            else if (kind == "C")
            {
                size_t      i = 0;
                size_t      j = 0;
                std::string sign;
                ls >> i >> j >> sign;
                int s = 0;
                if (sign == "lt")
                    s = -1;
                else if (sign == "eq")
                    s = 0;
                else if (sign == "gt")
                    s = 1;
                else
                    ADD_FAILURE() << "bad compare sign: " << sign;
                gv.compares.emplace_back(i, j, s);
            }
            else
            {
                ADD_FAILURE() << "unknown record kind: " << kind;
            }
        }
        return gv;
    }

    // compare(a, b) via the production comparator: -1 / 0 / +1.
    int
    comparator_sign(std::wstring const& a, std::wstring const& b)
    {
        m::case_insensitive_less<std::wstring> const less{};
        if (less(a, b))
            return -1;
        if (less(b, a))
            return 1;
        return 0;
    }
} // namespace

// Each `I` row's documented sort-key bytes are reproduced exactly in C++.
TEST(OrdinalGoldenVectors, SortKeyFormatParity)
{
    golden_vectors const gv = load_golden_vectors();
    ASSERT_FALSE(gv.corpus.empty());

    for (size_t i = 0; i < gv.corpus.size(); ++i)
    {
        EXPECT_EQ(compute_sort_key(gv.corpus[i]), gv.keys[i])
            << "sort-key mismatch at corpus index " << i;
    }
}

// Each `C` row's sign is reproduced by the production m::case_insensitive_less.
TEST(OrdinalGoldenVectors, ComparatorParity)
{
    golden_vectors const gv = load_golden_vectors();
    ASSERT_FALSE(gv.compares.empty());

    for (auto const& [i, j, expected] : gv.compares)
    {
        ASSERT_LT(i, gv.corpus.size());
        ASSERT_LT(j, gv.corpus.size());
        EXPECT_EQ(comparator_sign(gv.corpus[i], gv.corpus[j]), expected)
            << "comparator mismatch for pair (" << i << ", " << j << ")";
    }
}
