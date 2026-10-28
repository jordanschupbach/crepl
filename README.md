# crepl

A standalone C/C++ REPL implemented in C++, embedding Clang 21 and LLVM's JIT.
Definitions, variables, and objects persist between inputs. Earlier statements
and their side effects execute once.

```sh
nix develop path:.
just test
just run
```

```cpp
crepl> #include <print>
crepl> int count = 40;
crepl> count += 2;
crepl> std::print("count={}\n", count);
count=42
crepl> count
(int) 42
crepl> :quit
```

Compile with ordinary CMake when matching LLVM/Clang 21 development packages are
installed (including the `clang-cpp` and LLVM shared libraries):

```sh
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug --parallel
ctest --test-dir build/debug --output-on-failure
cmake --install build/debug --prefix "$HOME/.local"
cmake --build build/debug --target docs
```

Set `LLVM_DIR` and `Clang_DIR` to their CMake package directories when necessary.
Default input is C++23; `-x c` selects C17. `-std=...` overrides the default.
The implementation itself requires C++20. Unix is currently required.

## Use external libraries

```sh
crepl $(pkg-config --cflags --libs some-library)
# This redundant spelling is accepted too:
crepl -I $(pkg-config --cflags some-library) -L $(pkg-config --libs some-library)
```

`-I`, `-isystem`, `-D`, `-U`, `-include`, and Clang compiler flags configure the
parser. `-L` and `-l` load shared libraries into the JIT. `--load PATH` loads an
explicit shared object. Dependencies must be discoverable by the system loader
or the shared object's runtime paths. Static archives are not supported.

## Execute or step through a file

```sh
crepl --script statements.cpp
crepl --script statements.cpp -i
crepl --main example.cpp --step
```

Plain scripts contain includes, declarations, and statements at top level, without
a main wrapper. `--main` optionally removes a conventional single-line
`int main(...) {`, its matching closing brace on a separate line, and standalone
outer-body `return 0;` lines. Extracted variables become session globals.
Original source filenames and line numbers appear in diagnostics.

`--step` queues fragments without executing them. Type `:next` or press Enter to
execute one; inspect and modify variables between steps. Multiline declarations
and expressions are one fragment, executed when complete. `:abort` discards the
remaining queue. `:step FILE` and `:step-main FILE` queue another file.
`:load FILE` and `:load-main FILE` run files into the current session immediately.
Paths after these commands may contain spaces; no shell expansion is performed.

Standard input works for editor integrations that send code one physical line at
a time. Automatic continuation balances braces, parentheses, brackets, comments,
strings/raw strings, and conditional preprocessing. A trailing operator or
backslash continues input. Use `:begin` and `:end` for ambiguous constructs such
as a function signature followed by its opening brace on another line, or an
if/else whose parts should compile together. `:cancel` discards pending input.

A bare scalar expression prints its value; objects show their type and storage
address. A semicolon suppresses result printing.
`:help` lists commands. `:quit`, `%quit`, `.q`, or EOF exits. No implicit client
headers, namespaces, or libraries are loaded. No history file is written.

Compilation errors in a terminal allow further input. Batch input fails promptly
with nonzero status. Code executes inside crepl: exceptions are reported, but
signals, undefined behavior, or exit() can end the process. Undoing external
side effects, sandboxing, and crash recovery are not provided.

## Project layout and design

- `source/`, `include/crepl/`: frontend, lexical grouping, and embedded JIT.
- `tests/`: CTest frontend checks and executable/JIT integration tests.
- `cmake/`: configured interpreter toolchain defaults.
- `docs/plan.md`: staged implementation plan and validation record.
- `flake.nix`, `.envrc`, `justfile`: repeatable development and packaging.

Clang's [interpreter documentation](https://clang.llvm.org/docs/ClangRepl.html)
describes the incremental compilation model. crepl uses that library directly;
it does not launch clang-repl or replay a generated program per input.

## Editor integration

`--state-dir DIR` enables atomic file snapshots: `objects.tsv` contains escaped
name, type, and scalar-value columns; `definitions.txt` contains user declarations;
`history.txt` records successful input fragments; `inspect.txt` receives explicit
inspection results; and `status.txt` reports execution success or failure.
Backslash, tab, newline, and carriage return use `\\`, `\t`, `\n`, and `\r`
escapes in metadata rows. Headers and implicit compiler declarations are excluded.
Automatic scalar previews read stored arithmetic values without invoking getters
or copying user objects. `:inspect NAME` reports compiler-known record members.

`:evalat ROW FILE` executes the complete fragment containing a source line.
`:load-auto FILE` runs a script with optional conventional-main extraction.
These commands support paths containing spaces and let an editor submit a copy
of its live buffer without changing the user's original source.

`--display-function qualified::function` explicitly routes standalone
`object.show();` statements to `qualified::function(object)`. Comment/string
contents are preserved. The supplied adapter determines export and display;
crepl contains no client-library or editor-specific rendering implementation.
