// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

//
// Tools to decode PE (Portable Executable) images.
//
// No particular discrimination between PE / PE32+ is made, the code will
// attempt to be as compatible as possible with all variants available.
//
// Further, it avoids taking any dependencies on Windows header files as they
// are unnecessary and fill the namespace with ugly type definitions.
//
// Instead we will treat the file metadata as a persistence format, where
// in general we are interested in the data type and its offset from some
// base. Yes, a "struct" does encode that but the specific byte offset from
// the origin of the struct is implementation dependent. It seems
// unnecessary to take such a dependency.
//
// We may regret this, but it's the path chosen.
//

//
// The documentation does not really make it clear, but the e_lfanew
// in the image_dos_header gives the offset to either a
// image_nt_headers or an image_nt_headers64. You can't know which
// it is until you look at the Signature field to see whether it's
// 0x10b for PE32 or 0x20b for PE32+.
//

#include "image_data_directory.h"
#include "image_dos_header.h"
#include "image_file_header.h"
#include "image_import_descriptor.h"
#include "image_magic_t.h"
#include "image_nt_headers.h"
#include "image_optional_headers.h"
#include "image_section_header.h"
#include "offset_t.h"
#include "position_t.h"
