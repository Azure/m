// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <iostream>
#include <format>
#include <memory>
#include <string>
#include <string_view>

#include <m/byte_streams/byte_streams.h>
#include <m/byte_streams/memory_based_byte_streams.h>
#include <m/filesystem/filesystem.h>
#include <m/pe/pe.h>
#include <m/pe/pe_decoder.h>

using namespace std::string_view_literals;

// Demonstrate some basic assertions.
TEST(HelloTest, BasicAssertions)
{
    // Expect two strings not to be equal.
    EXPECT_STRNE("hello", "world");
    // Expect equality.
    EXPECT_EQ(7 * 6, 42);
}

TEST(OpeningANonPeFile, CheckForExceptionOnCreation)
{
    static std::array<unsigned char const, 38> not_a_pe_array{
        0x54, 0x68, 0x69, 0x73, 0x20, 0x69, 0x73, 0x20, 0x61, 0x20, 0x74, 0x65, 0x78,
        0x74, 0x20, 0x66, 0x69, 0x6C, 0x65, 0x2C, 0x20, 0x69, 0x74, 0x20, 0x69, 0x73,
        0x20, 0x6E, 0x6F, 0x74, 0x20, 0x61, 0x20, 0x50, 0x45, 0x2E, 0x0D, 0x0A,
    }; // 38 bytes total

    auto test_stream = m::byte_streams::make_memory_based_byte_stream(not_a_pe_array.data(),
                                                                      not_a_pe_array.size());

    // Should throw!
    EXPECT_THROW(auto pe = std::make_shared<m::pe::decoder>(test_stream), std::runtime_error);
}

TEST(OpeningASmallPeFile, BasicOpenTest)
{
    static std::array<unsigned char const, 1536> small_test_pe_array
#include "small_test_pe.h"
        ;

    auto test_stream = m::byte_streams::make_memory_based_byte_stream(small_test_pe_array.data(),
                                                                      small_test_pe_array.size());
    auto pe          = std::make_shared<m::pe::decoder>(test_stream);

    std::wcout << std::format(L"Here's a dump of a pe: {}\n", *(pe.get()));
}

TEST(OpeningHelloWorld, BasicOpenTest)
{
    static std::array<unsigned char const, 42496> hello_world_array
#include "helloworld.h"
        ;

    auto test_stream = m::byte_streams::make_memory_based_byte_stream(hello_world_array.data(),
                                                                      hello_world_array.size());
    auto pe          = std::make_shared<m::pe::decoder>(test_stream);

    std::wcout << std::format(L"Here's a dump of Hello World: {}\n", *(pe.get()));
}

TEST(TestFormatters, TestImageMagicFormatter)
{
    m::pe::image_magic_t magic = m::pe::image_magic_t::pe32;

    auto s1 = std::format(L"{}", magic);

    auto s2 = std::format(L"{}", m::pe::image_magic_t::pe32plus);
}

TEST(TestFormatters, TestImageFileHeaderCharacteristicFormatter)
{
    auto x = m::pe::image_file_header::characteristics{};

    auto s1 = std::format(L"{}", x);
}

TEST(TestFormatters, TestImageFileHeaderMachineFormatter)
{
    auto x = m::pe::image_file_header::machine_t{};

    auto s1 = std::format(L"{}", x);
}

TEST(TestFormatters, TestImageFileHeaderFormatter)
{
    auto x = m::pe::image_file_header{};

    auto s1 = std::format(L"{}", x);
}

TEST(TestFormatters, TestByteStreamPositionFormatter)
{
    auto x = m::io::position_t{};

    auto s1 = std::format(L"{}", x);
}

TEST(TestFormatters, TestRvaTFormatter)
{
    auto x = m::pe::rva_t{};

    auto s1 = std::format(L"{}", x);
}

TEST(TestFormatters, TestImageSectionHeaderCharacteristicsFormatter)
{
    auto x = m::pe::image_section_header::characteristics{};

    auto s1 = std::format(L"{}", x);
}
