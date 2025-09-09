// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <pugixml.hpp>

#ifdef PUGIXML_WCHAR_MODE

#define M_PUGIXML_T(x) L##x

#else

#define M_PUGIXML_T(x) x

#endif
