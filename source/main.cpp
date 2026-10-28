/// @file
/// @brief Interactive terminal, script execution, and source stepping.
#include <algorithm>
#include <crepl/input.hpp>
#include <crepl/options.hpp>
#include <crepl/session.hpp>
#include <deque>
#include <fstream>
#include <iostream>
#include <memory>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <unistd.h>
namespace crepl {
/// @brief Reads a file into complete units, reporting unfinished fragments.
static std::deque<Unit> read_units(const std::string& path, bool main_body) {
  std::ifstream file(path);
  if (!file)
    throw std::runtime_error("cannot open " + path);
  const auto lines = read_source(file, main_body);
  Input input;
  std::deque<Unit> units;
  for (std::size_t i = 0; i < lines.size(); ++i)
    if (auto unit = input.push(lines[i], path, i + 1))
      units.push_back(std::move(*unit));
  if (input.pending())
    throw std::runtime_error(path + ": incomplete fragment at end of file");
  return units;
}
/// @brief Detects the conventional main wrapper supported by read_source.
static bool has_main(const std::string& path) {
  std::ifstream file(path);
  std::string line;
  while (std::getline(file, line))
    if (std::regex_match(trim(line), std::regex(R"(int\s+main\s*\([^)]*\)\s*\{\s*(//.*)?)")))
      return true;
  return false;
}
/// @brief Runs queued fragments once, stopping before later code after a failure.
static bool run(Session& session, std::deque<Unit>& queue, bool one) {
  while (!queue.empty()) {
    Unit unit = std::move(queue.front());
    queue.pop_front();
    if (one)
      std::cerr << unit.file << ':' << unit.line << ":\n" << unit.code;
    if (!session.execute(unit))
      return false;
    if (one)
      break;
  }
  return true;
}
/// @brief Owns frontend input buffers and interactive script queue.
static int repl(Session& session, std::deque<Unit> queue, bool terminal) {
  Input input;
  bool explicit_block = false, had_error = false;
  std::string block;
  std::size_t number = 0, block_line = 0;
  for (std::string line;;) {
    if (terminal) {
      std::cerr << (explicit_block || input.pending() ? "...> " : "crepl> ");
      std::cerr.flush();
    }
    if (!std::getline(std::cin, line))
      break;
    ++number;
    const auto command = trim(line);
    if (command == ":quit" || command == "%quit" || command == ".q")
      break;
    if (command == ":cancel") {
      input.clear();
      explicit_block = false;
      block.clear();
      continue;
    }
    if (explicit_block) {
      if (command == ":end") {
        explicit_block = false;
        if (!session.execute({block, "<stdin>", block_line}))
          had_error = true;
        block.clear();
      } else
        block += line + '\n';
      continue;
    }
    if (!input.pending() && command.starts_with(':')) {
      const auto space = command.find(' ');
      const auto name = command.substr(0, space);
      const auto path = space == std::string::npos ? "" : trim(command.substr(space + 1));
      try {
        if (name == ":help")
          std::cout << usage();
        else if (name == ":begin") {
          explicit_block = true;
          block_line = number + 1;
        } else if (name == ":lib") {
          if (path.empty())
            throw std::runtime_error(":lib requires a path");
          if (!session.load(path))
            had_error = true;
        } else if (name == ":inspect") {
          if (!session.inspect(path))
            had_error = true;
        } else if (name == ":evalat") {
          std::istringstream args(path);
          std::size_t row;
          std::string source_path;
          if (!(args >> row))
            throw std::runtime_error(":evalat requires ROW FILE");
          std::getline(args, source_path);
          source_path = trim(source_path);
          auto units = read_units(source_path, has_main(source_path));
          bool selected = false;
          for (const auto& unit : units) {
            const auto length =
                static_cast<std::size_t>(std::count(unit.code.begin(), unit.code.end(), '\n'));
            if (row >= unit.line && row < unit.line + length) {
              if (!session.execute(unit))
                had_error = true;
              selected = true;
              break;
            }
          }
          if (!selected)
            std::cerr << "crepl: no executable fragment at line " << row << '\n';
        } else if (name == ":load-auto") {
          auto source = read_units(path, has_main(path));
          if (!run(session, source, false))
            had_error = true;
        } else if (name == ":abort")
          queue.clear();
        else if (name == ":next") {
          if (!run(session, queue, true))
            had_error = true;
        } else if (name == ":step" || name == ":step-main") {
          queue = read_units(path, name == ":step-main");
          std::cerr << "Queued " << queue.size()
                    << " fragments; :next or blank input executes one.\n";
        } else if (name == ":load" || name == ":load-main") {
          auto source = read_units(path, name == ":load-main");
          if (!run(session, source, false))
            had_error = true;
        } else
          throw std::runtime_error("unknown command " + name + "; use :help");
      } catch (const std::exception& error) {
        std::cerr << "crepl: " << error.what() << '\n';
        had_error = true;
      }
      if (had_error && !terminal)
        return 1;
      continue;
    }
    if (command.empty() && !input.pending() && !queue.empty()) {
      if (!run(session, queue, true)) {
        had_error = true;
        if (!terminal)
          return 1;
      }
      continue;
    }
    if (auto unit = input.push(line, "<stdin>", number)) {
      if (!session.execute(*unit)) {
        had_error = true;
        if (!terminal)
          return 1;
      }
    }
  }
  if (input.pending() || explicit_block) {
    std::cerr << "crepl: incomplete fragment at end of input\n";
    return 1;
  }
  return terminal ? 0 : (had_error ? 1 : 0);
}
} // namespace crepl
/// @brief Configures a standalone incremental session; returns nonzero on batch failure.
int main(int argc, const char** argv) {
  try {
    const auto options = crepl::parse_options(argc, argv);
    if (options.help) {
      std::cout << crepl::usage();
      return 0;
    }
    if (options.version) {
      std::cout << "crepl 0.1.0 (LLVM/Clang 21)\n";
      return 0;
    }
    const bool terminal = ::isatty(STDIN_FILENO);
    std::deque<crepl::Unit> queue;
    if (!options.script.empty())
      queue = crepl::read_units(options.script, options.extract_main);
    crepl::Session session(options);
    if (!options.step && !crepl::run(session, queue, false))
      return 1;
    if (!options.script.empty() && !options.interactive)
      return 0;
    if (terminal)
      std::cerr << "crepl: C" << (options.c ? "17" : "++")
                << " incremental session; :help for commands\n";
    return crepl::repl(session, std::move(queue), terminal);
  } catch (const std::exception& error) {
    std::cerr << "crepl: " << error.what() << '\n';
    return 1;
  }
}
