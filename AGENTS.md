# Agent Guidelines & Development Standards for RingCache

This document defines persistent instructions, engineering standards, and verification protocols for AI coding assistants and contributors working on **RingCache**.

---

## 1. Core Technical Standards

- **Language Standard**: C++17 (`-std=c++17`). Use modern C++ features, idioms, and standard library utilities throughout.
- **Build System**: Modern target-based CMake (minimum version 3.16).
  - Explicitly declare build targets and dependencies using `target_include_directories`, `target_link_libraries`, and `target_compile_features`.
  - Avoid obsolete global directory modifications (`include_directories`, `link_libraries`).
  - Propose the minimal `CMakeLists.txt` build skeleton and test setup as a distinct, reviewable step before writing cache implementation code.
- **Dependencies**: Keep external dependencies minimal. Prefer the C++ Standard Library (`std::`) whenever practical. Propose and justify any external library additions (e.g., test framework, networking abstraction) before introducing them.
- **Code Readability & Maintainability**:
  - Prioritize clear, self-documenting, and maintainable code over premature micro-optimizations.
  - Follow consistent naming conventions: `PascalCase` for classes/structs/types, `camelCase` or `snake_case` for methods/variables (kept uniform within each component), and `UPPER_CASE` for constants/macros.
  - Const-correctness: Mark methods, parameters, and local variables `const` whenever they are not mutated.

---

## 2. Concurrency and Memory Safety

- **Memory Management**:
  - Strictly adhere to RAII (Resource Acquisition Is Initialization).
  - Zero naked allocations: Do not use raw `new` and `delete`. Manage resource lifetimes using `std::unique_ptr` and `std::shared_ptr`.
  - Maintain clear ownership boundaries. Use non-owning raw pointers, references, or `std::string_view` only when lifetimes are strictly guaranteed by the caller.
- **Concurrency & Synchronization**:
  - Design with thread safety as a first-class requirement. Eliminate hidden or unprotected mutable state.
  - Protect shared structures using standard synchronization primitives (`std::mutex`, `std::shared_mutex`, `std::unique_lock`, `std::shared_lock`, `std::atomic`).
  - Keep critical sections minimal. Never perform I/O operations or lengthy computations while holding locks.
  - Guard against deadlocks and lock inversion. Explicitly document lock acquisition order whenever multiple locks must be acquired.
  - Ensure zero data races, zero undefined behavior, and clean execution under address and thread sanitizers (ASan/TSan).

---

## 3. Working Principles for Agents

### 3.1 Plan Before Implementation
- For any significant feature, refactoring, or architectural change, present a concise, step-by-step plan before writing or altering code.
- Explicitly explain design choices, trade-offs, and assumptions so the maintainer can evaluate the rationale.
- Clearly state which files and components will be affected.

### 3.2 Focused, Atomic Changes
- Make focused, minimal edits that address the immediate task.
- Do not perform broad unsolicited refactorings, style overhauls, or modify unrelated files.
- Preserve existing comments, docstrings, and established code patterns.

### 3.3 Test-Driven Verification
- Write comprehensive unit or integration tests for every new feature, bug fix, or edge case.
- **Verification Rule**: Never claim a command, build, or test succeeded unless it was actually executed and the exit status and output were verified.
- If an environment prevents executing a build or test command, explicitly state that it has not been verified.
- When resolving test failures, address the underlying cause rather than weakening or removing test assertions.

### 3.4 Documentation Maintenance
- Maintain documentation in lockstep with codebase changes.
- Update [docs/DESIGN_DECISIONS.md](docs/DESIGN_DECISIONS.md) whenever architectural choices or protocols are created, revised, or validated.
- Keep [README.md](README.md) aligned with the true state of implemented versus planned features.
- Mark all unvalidated or unapproved architectural decisions as **Proposed**.
