/// @file
/// @brief Command-line configuration independent of LLVM headers.
#pragma once
#include <string>
#include <vector>
namespace crepl {
/// @brief Owned settings for one interpreter process.
struct Options {
  std::vector<std::string> compiler;     ///< Clang driver flags, excluding linker flags.
  std::vector<std::string> library_dirs; ///< Explicit dynamic-library search paths.
  std::vector<std::string> libraries;    ///< -l names or explicit shared objects, in order.
  std::string state_dir; ///< Optional editor snapshots directory; no client-specific protocol.
  std::string display_function; ///< Optional qualified function replacing standalone object.show().
  std::string script;           ///< Optional source filename; empty reads standard input.
  bool extract_main = false;    ///< Remove only a conventional main wrapper.
  bool interactive = false;     ///< Continue interactively after a script.
  bool step = false;            ///< Queue script fragments for :next.
  bool c = false;               ///< C mode rather than default C++23.
  bool help = false;            ///< Print usage without creating a compiler.
  bool version = false;         ///< Print version without creating a compiler.
};
/// @brief Parses flags; throws std::runtime_error for invalid or missing arguments.
/// Accepts ordinary pkg-config output and redundant -I -Ipath / -L -Lpath prefixes.
Options parse_options(int argc, const char* const* argv);
/// @brief Returns the command-line and interactive-command reference.
std::string usage();
} // namespace crepl
