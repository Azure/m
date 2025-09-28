# NuGet Package Generation

This repository now supports generating NuGet packages using CMake and CPack.

## Prerequisites

- CMake 3.23 or later
- CPack with NuGet generator support
- Visual Studio or compatible toolchain for Windows builds

## Building NuGet Packages

1. Configure the project with CMake:
   ```bash
   cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
   ```

2. Build the project:
   ```bash
   cmake --build build --config Release
   ```

3. Generate the NuGet package:
   ```bash
   cpack --config build/CPackConfig.cmake -G NuGet
   ```

## Package Configuration

The NuGet package is configured with the following details:

- **Package Name**: `Microsoft.M`
- **Version**: Automatically uses the project version (currently 0.0.10)
- **Description**: "Microsoft's M library - A collection of C++ utilities for Windows and cross-platform development"
- **Authors/Owners**: Microsoft
- **Homepage**: https://github.com/Azure/m
- **License**: https://github.com/Azure/m/blob/main/LICENSE
- **Tags**: cpp, utilities, microsoft, native, cross-platform

## Multiple Package Formats

The project supports multiple packaging formats simultaneously:

- **NuGet**: For .NET ecosystem integration
- **NSIS**: Windows installer
- **ZIP**: Cross-platform archive

To generate all package formats:
```bash
cpack --config build/CPackConfig.cmake
```

To generate only NuGet packages:
```bash
cpack --config build/CPackConfig.cmake -G NuGet
```

## Customizing Package Metadata

The NuGet package metadata can be customized by modifying the CPack configuration variables in `CMakeLists.txt`:

- `CPACK_NUGET_PACKAGE_NAME`: Package identifier
- `CPACK_NUGET_PACKAGE_VERSION`: Package version
- `CPACK_NUGET_PACKAGE_DESCRIPTION`: Package description
- `CPACK_NUGET_PACKAGE_AUTHORS`: Package authors
- `CPACK_NUGET_PACKAGE_OWNERS`: Package owners
- `CPACK_NUGET_PACKAGE_HOMEPAGE_URL`: Homepage URL
- `CPACK_NUGET_PACKAGE_LICENSEURL`: License URL
- `CPACK_NUGET_PACKAGE_TAGS`: Searchable tags
- `CPACK_NUGET_PACKAGE_DEPENDENCIES`: Package dependencies

## Notes

- The project is primarily designed for MSVC on Windows
- Linux/GCC builds may require additional configuration for dependencies
- Ensure all dependencies (like pugixml) are properly configured before building