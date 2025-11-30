// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <functional>
#include <iterator>
#include <numeric>
#include <utility>

#include <m/cast/to.h>
#include <m/linux_strings/convert.h>
#include <m/strings/convert.h>
#include <m/strings/tstring.h>
#include <m/utf/decode.h>
#include <m/utf/encode.h>
#include <m/utf/transcode.h>
#include <m/utility/make_span.h>
#include <m/utility/pointers.h>
#include <m/utility/zstring.h>

namespace m::conversion_details
{
    template <typename ToCharT, typename FromT>
        requires(m::character<ToCharT>)
    std::basic_string<ToCharT>
    transcode_to(FromT&& from)
    {
        return utf::transcode<ToCharT>(from);
    }

} // namespace m::conversion_details
