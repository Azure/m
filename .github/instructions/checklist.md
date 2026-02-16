# Repository Instructions Checklist

This document tracks improvements and additions needed for the repository's coding guidelines and standards.

## High Priority

### Layering Architecture

- [ ] **Define Layer 1**: Identify and document the next layer above Foundation (Layer 0)
  - Candidates: byte_streams, memory, utility, strings?
  - Document which libraries belong at this layer
  
- [ ] **Define Layer 2**: Identify mid-level infrastructure libraries
  - Candidates: filesystem, tracing, threadpool?
  - Document dependencies and allowed references
  
- [ ] **Define Layer 3+**: Continue defining layers as needed
  - Application-level libraries
  - Platform-specific implementations
  
- [ ] **Document pe library layer**: Classify where `src/libraries/pe` belongs in the layering hierarchy
  - Appears to depend on byte_streams, memory, filesystem
  - Should be above Layer 0 but needs specific assignment

- [ ] **Add layering validation**: Consider automated checks to enforce layering rules
  - CMake script to verify dependencies don't violate layering
  - CI/CD integration to catch violations

### Test Framework Guidelines

- [ ] **Add GTest organization standards**
  - Test file naming conventions (e.g., `test_*.cpp` vs `*_test.cpp`)
  - Test suite naming patterns
  - Test case naming patterns (e.g., `ComponentName_MethodName_ExpectedBehavior`)
  
- [ ] **Document test structure patterns**
  - Arrange-Act-Assert pattern
  - Given-When-Then pattern
  - Fixture usage guidelines
  
- [ ] **Add test coverage expectations**
  - Minimum coverage requirements per library
  - Critical path coverage requirements
  - Error condition testing requirements
  
- [ ] **Document test categories**
  - Unit tests vs integration tests
  - Platform-specific tests
  - Performance/stress tests
  - Where to place each category
  
- [ ] **Add mocking/faking guidelines**
  - When to use mocks vs fakes vs real implementations
  - Recommended mocking frameworks (if any)
  - Interface design for testability

## Medium Priority

### C++ Guidelines Expansion

- [ ] **Add guidelines for C++20 concepts**
  - When to use concepts vs SFINAE
  - Naming conventions for concepts
  - Concept composition patterns
  
- [ ] **Add guidelines for C++20 ranges**
  - When to use ranges vs traditional algorithms
  - Range adaptor usage patterns
  - Performance considerations
  
- [ ] **Add guidelines for C++20 modules** (if/when adopted)
  - Module organization
  - Import patterns
  - Migration from headers
  
- [ ] **Add error handling patterns**
  - When to use exceptions vs error codes vs std::expected (C++23)
  - Custom exception hierarchy guidelines
  - Error propagation patterns

### Platform Portability

- [ ] **Document platform abstraction patterns**
  - How to structure platform-specific code
  - Naming conventions for platform implementations
  - When to use runtime vs compile-time dispatch
  
- [ ] **Add cross-platform testing requirements**
  - Which platforms must be tested
  - Platform-specific test exclusions
  - CI/CD matrix configuration

### Documentation Standards

- [ ] **Add Doxygen style guide**
  - Required tags for public APIs (@brief, @param, @return, etc.)
  - Documentation coverage requirements
  - Examples and code snippets guidelines
  
- [ ] **Add README requirements**
  - What every library README should contain
  - Examples of good library documentation
  - API surface overview requirements

## Low Priority

### Code Style Details

- [ ] **Add formatting standards**
  - Indentation (spaces vs tabs, size)
  - Brace style
  - Line length limits
  - Reference .clang-format if it exists
  
- [ ] **Add naming convention details**
  - Already have m_ for members, but document fully
  - Namespace naming
  - Template parameter naming
  - Constant naming (k_ prefix vs K_ vs UPPER_CASE)
  
- [ ] **Add include order standards**
  - Standard library includes first vs project includes first
  - Alphabetical ordering within groups
  - Forward declaration guidelines

### Build System

- [ ] **Add CMake best practices**
  - Target naming conventions
  - Property propagation (INTERFACE vs PUBLIC vs PRIVATE)
  - Install/export requirements
  - Subdirectory organization
  
- [ ] **Add warning level guidance**
  - Default warning levels
  - How to enable warnings as errors
  - How to suppress specific warnings (and when it's acceptable)

## Future Considerations

- [ ] **Add performance guidelines**
  - When to profile before optimizing
  - Common performance pitfalls
  - Memory allocation patterns
  
- [ ] **Add security guidelines**
  - Input validation requirements
  - Buffer overflow prevention
  - Integer overflow prevention
  - Dependencies on security-critical paths

## Notes

This checklist should be reviewed and updated periodically as the repository evolves. Items can be promoted or demoted in priority based on actual pain points encountered during development.

When adding new guidelines:
1. Keep them concise and actionable
2. Provide examples of good and bad patterns
3. Explain the "why" behind the guideline, not just the "what"
4. Reference external resources where appropriate (C++ Core Guidelines, etc.)
