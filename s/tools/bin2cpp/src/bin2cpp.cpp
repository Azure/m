// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <iostream>

constexpr static size_t buffer_length = 64 * 1024;

int
main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << std::format("Usage: {} filename\n", argv[0]);
        std::exit(EXIT_FAILURE);
    }

    std::FILE* fp = std::fopen(argv[1], "rb");
    if (!fp)
    {
        std::perror("Unable to open input file");
        std::exit(EXIT_FAILURE);
    }

    std::array<std::byte, buffer_length> buffer;
    std::cout << "{\n";

    std::size_t bytes_read{};
    std::size_t n{};

    while ((bytes_read = std::fread(buffer.data(), sizeof(buffer[0]), buffer.size(), fp))> 0)
    {
        for (std::size_t i = 0; i < bytes_read; i++)
        {
            if (++n % 10)
            {
                std::cout << std::format("{:#02x}, ", static_cast<uint8_t>(buffer[i]));
            }
            else
            {
                std::cout << std::format("{:#02x},\n", static_cast<uint8_t>(buffer[i]));
            }
        }
    }

    std::fclose(fp);
    std::cout << std::format("\n}}; // {} bytes total\n", n);
    return EXIT_SUCCESS;
}
