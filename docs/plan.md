# Implementation plan

The deliverable is an independent C++ executable using the public Clang
incremental compiler and LLVM ORC JIT APIs. It never depends on a client library.
The compiler retains definitions, object storage, templates, and generated code
between fragments. Previous input is never recompiled or replayed for execution.

1. Establish CMake, CTest, Doxygen, Nix, just, and direnv workflows. Pin a matching
   LLVM/Clang 21 toolchain; capture its system headers for the embedded compiler.
2. Implement compiler arguments, include paths, macros, standards, and ordered
   shared-library loading. Accept pkg-config output, including redundant -I/-L
   prefixes. Keep frontend and LLVM-dependent code separate.
3. Add physical-line grouping, continuation prompts, source locations, script
   execution, and stepping. Preserve declarations and side effects between inputs.
   Provide explicit multiline entry for constructs beyond lexical grouping.
4. Validate state, non-replayed side effects, standard containers, C++23 printing,
   C mode, functions, multiline input, errors, and external shared libraries.
5. Test a real downstream example externally, both as extracted statements and
   through optional conventional-main extraction, including an interactive window.
6. Add the executable as a local dependency in the downstream Nix environment.
   Keep its input as a path checkout so no commits are required while developing.

## Scope and constraints

Automatic grouping is lexical, not a second C++ parser. It handles nesting,
comments, strings, raw strings, conditional preprocessing, and continued
expressions. `:begin`/`:end` or backslash continuation covers split function
signatures, if/else boundaries, and other ambiguous physical-line breaks.

`--main` is an explicitly requested convenience for conventional examples with
`int main(...) {` and its final brace on separate lines. It removes standalone
`return 0;` at outer main-body depth. It does not promise to transform arbitrary
programs, handle arguments/early returns, or preserve local-variable semantics:
extracted declarations become session globals.

Execution occurs in process. Heap/object state persists, normal teardown runs
registered destructors, and compilation errors do not undo prior side effects.
Batch processing stops at the first error. Interactive parsing errors allow
correction. Fatal signals, undefined behavior, exit(), and some JIT failures can
end or invalidate the session. Static archives, a debugger, concurrent sessions,
remote execution, and arbitrary compiler-version combinations are future work.

## Validation record

Verified on x86_64-linux with LLVM/Clang 21.1.8 and libstdc++ 15.2:

- CMake debug build and both CTest groups pass. The frontend group covers argument
  normalization, grouping, and main extraction. The executable group contains 21
  integration tests, including TTY error recovery, live stdin execution before
  EOF, state between script steps, exactly-once side effects, C17, C++23 printing,
  shared libraries, exceptions, and normal object destruction.
- The standalone Nix package builds and runs CTest in the build sandbox.
  `nix develop path:. --command just test` runs the documented development workflow.
- `just docs` generates HTML/XML without Doxygen warnings; source members have
  descriptions, including private implementation state.
- A downstream Iris plotting example was sent through stdin one physical line at
  a time, with synchronization checks between increments and only the conventional
  main wrapper/return removed. Using compiler/library flags from pkg-config,
  it printed the dataframe, opened its native plot on a temporary X11 display,
  and exited successfully after a normal window close. A separate headless run
  produced SVG. This integration lives outside crepl: there is no dependency on
  the downstream library, headers, repository path, or dataset.
- The downstream flake imports this uncommitted checkout as a local path input
  and exposes crepl in its C++ and default development shells.

No repository commits are needed for these workflows. A downstream locked path
input can be refreshed with `nix flake update crepl` after local edits.

Other host platforms are declared by the Nix flake but have not been tested here.

## Generic editor integration

Added opt-in atomic object/type/scalar/history snapshots, explicit compiler-member
inspection, cursor-fragment execution, and conventional-main auto-loading.
A configurable display function lets a client supply figure-export adapters;
crepl has no dependency on the client editor or plotting library.
The executable integration suite now includes metadata isolation, cursor execution,
main extraction, native/default versus adapted display, and raw-string preservation.
