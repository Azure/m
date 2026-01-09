// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

#include <Windows.h>

#include <m/multi_byte/code_page.h>
#include <m/errors/errors.h>
#include <m/strings/convert.h>
#include <m/utf/decode.h>
#include <m/utf/encode.h>
#include <m/utility/concepts.h>
#include <m/utility/make_span.h>

#include <m/cp_acp/cp_acp.h>

namespace m
{
    template <typename TCharOut>
        requires utf16_character<TCharOut>
    void
    acp_to_tstring(std::string_view in, std::basic_string<TCharOut>& out);
} // namespace m
