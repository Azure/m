# Repository-wide instructions for copilot

Ensure that the code always builds clean for debug and retail builds.

Unit tests must pass to consider a unit of work complete once the code has built.

## Definition of "Complete" for refactorings and code changes

Work is NOT complete until ALL of the following criteria are met:
1. **Debug build**: Code builds successfully with `--preset x64-debug` (or equivalent)
2. **Retail/Release build**: Code builds successfully with `--preset x64-release` (or equivalent)
3. **Debug tests**: All unit tests pass in debug configuration
4. **Retail tests**: All unit tests pass in release/retail configuration

Do NOT declare work "complete" or "successful" until all four criteria above are verified.
If time constraints prevent full verification, explicitly state which configurations remain untested.

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

