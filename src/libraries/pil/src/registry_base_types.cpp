// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <m/pil/registry.h>
#include <m/strings/convert.h>
#include <m/utility/make_span.h>

using namespace std::string_view_literals;

namespace m::pil::registry
{
    registry_storage_string
    to_double_null_terminated_registry_storage_string(std::vector<registry_string> const& v)
    {
        registry_storage_string retval{};

        // We keep a temp string around so we can reuse the allocations over all the conversions
        registry_storage_string temp{};

        for (auto&& e: v)
        {
            to_null_terminated_registry_storage_string(registry_string_view(e.begin(), e.end()), temp);
            retval.append(temp);
        }

        retval.append(u"\0"sv);

        return retval;
    }

} // namespace m::pil::registry
