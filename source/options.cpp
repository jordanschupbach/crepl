/// @file
/// @brief Parses compiler, library, script, and frontend options.
#include <crepl/options.hpp>
#include <stdexcept>
namespace crepl {
Options parse_options(int argc, const char* const* argv) {
  Options result;
  /// @brief Consumes a required command-line value or reports the missing argument.
  auto value = [&](int& i, const std::string& flag) -> std::string {
    if (++i >= argc || std::string(argv[i]).empty())
      throw std::runtime_error("missing value after " + flag);
    return argv[i];
  };
  /// @brief Records the single optional script path, rejecting conflicting inputs.
  auto script = [&](const std::string& path) {
    if (!result.script.empty())
      throw std::runtime_error("only one script can be specified");
    result.script = path;
  };
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h")
      result.help = true;
    else if (arg == "--version")
      result.version = true;
    else if (arg == "-i" || arg == "--interactive")
      result.interactive = true;
    else if (arg == "--step")
      result.step = result.interactive = true;
    else if (arg == "--main") {
      result.extract_main = true;
      script(value(i, arg));
    } else if (arg == "--script")
      script(value(i, arg));
    else if (arg == "--") {
      while (++i < argc)
        script(argv[i]);
    } else if (arg == "-I" || arg == "-L") {
      auto next = value(i, arg);
      if (next.starts_with(arg)) {
        --i;
        continue;
      }
      if (next.starts_with('-'))
        throw std::runtime_error("missing directory after " + arg);
      if (arg == "-I") {
        result.compiler.push_back(arg);
        result.compiler.push_back(next);
      } else
        result.library_dirs.push_back(next);
    } else if (arg.starts_with("-L"))
      result.library_dirs.push_back(arg.substr(2));
    else if (arg == "-l")
      result.libraries.push_back(value(i, arg));
    else if (arg.starts_with("-l"))
      result.libraries.push_back(arg.substr(2));
    else if (arg == "--state-dir")
      result.state_dir = value(i, arg);
    else if (arg == "--display-function")
      result.display_function = value(i, arg);
    else if (arg == "--load")
      result.libraries.push_back(value(i, arg));
    else if (arg == "-x") {
      auto language = value(i, arg);
      if (language != "c" && language != "c++")
        throw std::runtime_error("-x accepts c or c++");
      result.c = language == "c";
    } else if (arg == "-isystem" || arg == "-iquote" || arg == "-include" || arg == "-D" ||
               arg == "-U" || arg == "-resource-dir" || arg == "--sysroot") {
      result.compiler.push_back(arg);
      result.compiler.push_back(value(i, arg));
    } else if (arg.starts_with("-Wl,")) {
      // pkg-config may include runtime paths; dynamic libraries need no link step.
      if (arg.starts_with("-Wl,-rpath,"))
        result.library_dirs.push_back(arg.substr(11));
      else
        throw std::runtime_error("unsupported linker flag " + arg +
                                 "; use shared libraries with -L/-l");
    } else if (!arg.empty() && arg.front() == '-')
      result.compiler.push_back(arg);
    else if (arg.ends_with(".so") || arg.ends_with(".dylib") ||
             arg.find(".so.") != std::string::npos)
      result.libraries.push_back(arg);
    else
      script(arg);
  }
  bool standard = false;
  for (const auto& arg : result.compiler)
    if (arg.starts_with("-std="))
      standard = true;
  result.compiler.push_back(result.c ? "-xc" : "-xc++");
  if (!standard)
    result.compiler.push_back(result.c ? "-std=c17" : "-std=c++23");
  return result;
}
std::string usage() {
  return R"(Usage: crepl [compiler flags] [-Ldir] [-lname] [--script FILE | --main FILE] [-i]

C++23 by default; -x c selects C17. Accepts -I/-isystem/-D/-U/-std= flags.
  crepl $(pkg-config --cflags --libs some-library)
  crepl -I $(pkg-config --cflags some-library) -L $(pkg-config --libs some-library)
  crepl --main example.cpp --step
  crepl --script snippets.cpp -i

Input executes incrementally; multiline fragments are grouped automatically.
Use a trailing backslash or :begin / :end for explicit continuation.
A bare expression prints its value; a semicolon suppresses value printing.
--main removes a conventional int main(...) { wrapper and standalone return 0;.
Scripts stop on the first error. -i preserves script state for an interactive session.

Editor integration: --state-dir DIR writes atomic objects.tsv, definitions.txt,
history.txt, inspect.txt and status.txt snapshots. --display-function NAME opts
into routing standalone object.show() statements to NAME(object).

Interactive commands (also accepted on standard input):
  :help                 Show this reference
  :quit                 Exit (also %quit or .q)
  :cancel               Discard an incomplete fragment
  :begin / :end         Explicit multiline fragment
  :load FILE            Execute a plain script into this session
  :load-main FILE       Execute a conventional main body into this session
  :step FILE            Queue a plain script, without executing it
  :step-main FILE       Queue a conventional main body
  :next                 Execute the next queued fragment (blank input also steps)
  :abort                Discard the queued script
  :inspect NAME         Inspect a live variable and public record fields
  :load-auto FILE       Load a script, removing a conventional main if present
  :evalat ROW FILE      Execute the complete fragment containing source ROW
  :lib PATH             Load a shared library into this session

Code runs in this process. Mutations and external side effects happen once and
are not reversible. A fatal signal or exit() in submitted code ends crepl.
)";
}
} // namespace crepl
