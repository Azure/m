// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/strings/convert.h>

#ifdef WIN32

#include <m/windows_strings/convert.h>

#else

#include <m/linux_strings/convert.h>

#endif
