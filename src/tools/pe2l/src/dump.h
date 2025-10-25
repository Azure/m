// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <filesystem>
#include <functional>

#include <m/command_options/command_options.h>

namespace m
{
    namespace pe2l
    {
        void
        dump(m::not_null<m::command_options::parsed_command<char>*> pc);
    } // namespace pe2l
} // namespace m