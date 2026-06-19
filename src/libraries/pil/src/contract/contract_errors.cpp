// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "contract_errors.h"

#include <string>
#include <system_error>

namespace m::pil
{
    namespace
    {
        class contract_error_category_impl final : public std::error_category
        {
        public:
            char const*
            name() const noexcept override
            {
                return "m::pil::contract";
            }

            std::string
            message(int value) const override
            {
                switch (static_cast<contract_error>(value))
                {
                case contract_error::request_violation:
                    return "request violates the bound HTTP contract";
                case contract_error::response_violation:
                    return "response violates the bound HTTP contract";
                }
                return "unknown contract error";
            }
        };
    } // namespace

    std::error_category const&
    contract_error_category() noexcept
    {
        static contract_error_category_impl const category;
        return category;
    }

    std::error_code
    make_error_code(contract_error e) noexcept
    {
        return {static_cast<int>(e), contract_error_category()};
    }
} // namespace m::pil
