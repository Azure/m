# CMake Build System Notes

## CMakePresets.json

### Toolchain file duplication

The `base` preset specifies the vcpkg toolchain file in two places:

```json
"toolchainFile": "${sourceDir}/vcpkg/scripts/buildsystems/vcpkg.cmake",
"cacheVariables": {
    "CMAKE_TOOLCHAIN_FILE": "${sourceDir}/vcpkg/scripts/buildsystems/vcpkg.cmake",
    ...
}
```

This is intentional. The `toolchainFile` preset property is the modern CMake way
and is applied automatically. The `CMAKE_TOOLCHAIN_FILE` cache variable is
redundant but exists for:

- IDEs that query the CMake cache directly rather than parsing the preset
- Tools that inspect `CMakeCache.txt` to find the toolchain
- Compatibility when inheriting presets that might override `toolchainFile`

Both must point to the same file.
