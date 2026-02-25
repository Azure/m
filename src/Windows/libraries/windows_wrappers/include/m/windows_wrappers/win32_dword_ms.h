// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#undef NOMINMAX
#define NOMINMAX

#include <Windows.h>

#include <m/wrappers/wrapper_template.h>

namespace m
{
    using win32_dword_ms = nonscalar_wrapper<DWORD, struct tag_dword_as_milliseconds>;
} // namespace m
