// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>

constexpr static size_t buffer_length = 64 * 1024;

int
main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "Usage: %s filename\n", argv[0]);
        std::exit(EXIT_FAILURE);
    }

    std::FILE* fp = std::fopen(argv[1], "rb");
    if (!fp)
    {
        std::perror("Unable to open input file");
        std::exit(EXIT_FAILURE);
    }

    std::fclose(fp);
    std::printf("\n}; // 0 bytes total\n");
    return EXIT_SUCCESS;
}

#ifdef WIN32

__declspec(dllexport) int
do_something(uint32_t, uint64_t)
{
    return 42;
}

__declspec(dllexport) int
do_something_else(uint32_t, uint64_t)
{
    return 42;
}

__declspec(dllexport) int
do_something_altogether_different(uint32_t, uint64_t)
{
    return 42;
}

#endif
