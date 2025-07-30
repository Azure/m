// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <functional>

#include <gtest/gtest.h>

namespace m::googletest
{
    class on_execution_end : public testing::EmptyTestEventListener
    {
    public:
        template <typename Callable>
        constexpr on_execution_end(Callable callable): m_toCall(callable)
        {}

        void
        OnTestEnd(testing::TestInfo const& test_info) override
        {
            m_toCall(test_info.result()->Passed());
        }

    private:
        std::function<void(bool)> m_toCall;
    };
} // namespace m::googletest
