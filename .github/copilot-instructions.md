# Repository-wide instructions for copilot

## ?? CRITICAL: PROJECT BUILD SYSTEM ??

**THIS IS A CMAKE C++ PROJECT - NOT A .NET PROJECT**

### ? NEVER DO THIS:
- DO NOT run `dotnet clean`, `dotnet build`, `dotnet test`, or ANY `dotnet` commands
- DO NOT look for or reference `.sln` files or `.csproj` files  
- DO NOT assume this is a Visual Studio .NET/C# project
- DO NOT use MSBuild commands

### ? ALWAYS USE CMAKE:
This project ONLY uses **CMake** for building and **CTest** for testing.

See the "CMake Build Commands Reference" section below for correct commands.

---

Ensure that the code always builds clean for debug and retail builds.

Unit tests must pass to consider a unit of work complete once the code has built.

---

## Definition of "Complete" for refactorings and code changes

**MANDATORY**: Work is NOT complete until ALL of the following criteria are met:
1. **Debug build**: Code builds successfully with `--preset x64-debug` (or equivalent)
2. **Retail/Release build**: Code builds successfully with `--preset x64-release` (or equivalent)
3. **Debug tests**: All unit tests pass in debug configuration
4. **Retail tests**: All unit tests pass in release/retail configuration

**CRITICAL REQUIREMENT**: Do NOT declare work "complete" or "successful" until all four criteria above are verified.

**For ALL refactorings**: These verification steps MUST be completed as part of the refactoring work. 
Refactoring is not complete until all builds pass and all tests pass in both debug and release configurations.

If time constraints prevent full verification, explicitly state which configurations remain untested and why the work should NOT be considered complete.

---

## CMake Build Commands Reference

### When User Says "Rebuild Clean" or "Clean Build and Test"

**ALWAYS use these CMake commands:**

1. **Clean the build:**
   ```bash
   cmake --build --preset=windows-default --target clean
   ```

2. **Rebuild:**
   ```bash
   cmake --build --preset=windows-default
   ```

3. **Run tests:**
   ```bash
   ctest --preset=windows-default
   ```

### Standard CMake Workflow

**Configure** the build using one of the presets:
```bash
cmake --preset x64-debug
# or
cmake --preset x64-release
```

**Build** using the configured preset:
```bash
cmake --build --preset x64-debug
# or
cmake --build --preset x64-release
```

**Test** using CTest:
```bash
ctest --preset=windows-default
```

---

## User preferences

### Concise responses (no excessive praise or commentary)

Users with GitHub identifiers in this list prefer concise, direct responses:

- EmJayGee

**To add yourself**: Add your GitHub username to the list above with a single line: `- YourGitHubUsername`

# C++ language guidance

See `.github/instructions/cxx.instructions.md` for detailed C++ coding standards including:
- Language standard usage (C++20/C++23)
- Casting rules
- Memory safety requirements
- Commenting guidelines

# Component layering guidance
See `.github/instructions/layering.instructions.md` for guidance on component layering, including:
- Allowed dependencies between layers
- Best practices for maintaining clean architecture
- Examples of proper layering and dependency management

# Building and other context

## vcpkg

The vcpkg github repository is a submodule of `m` immediately at the top level of the repo.

It has to be "bootstrapped" by running either "vcpkg/bootstrap.cmd" on Windows or "vcpkg/bootstrap.sh" on Linux.

