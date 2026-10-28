set positional-arguments
build_dir := "build/debug"

# Show available workflows.
default:
    @just --list

# Configure with the LLVM/Clang development packages in the Nix shell.
configure:
    cmake -S . -B {{build_dir}} -DCMAKE_BUILD_TYPE=Debug

build: configure
    cmake --build {{build_dir}} --parallel

test: build
    ctest --test-dir {{build_dir}} --output-on-failure

run *args: build
    {{build_dir}}/crepl "$@"

docs: configure
    cmake --build {{build_dir}} --target docs

# Build the independently packaged executable, including CTest.
package:
    nix build "path:$(pwd -P)"
