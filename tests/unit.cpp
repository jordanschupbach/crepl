/// @file
/// @brief Standalone frontend tests; failures remain enabled in Release builds.
#include <crepl/input.hpp>
#include <crepl/options.hpp>
#include <iostream>
#include <sstream>
#include <stdexcept>
/// @brief Fails a check without relying on NDEBUG-sensitive assertions.
static void check(bool value) {
  if (!value)
    throw std::runtime_error("frontend check failed");
}
/// @brief Exercises lexical grouping, wrapper extraction, and argument normalization.
int main() {
  crepl::Input input;
  check(!input.push("auto x = f(", "test", 1));
  check(!input.push("  1,", "test", 2));
  auto call = input.push("  2);", "test", 3);
  check(call && call->line == 1 && call->file == "test");
  check(!input.pending());
  check(!input.push("// comment {", "test", 4));
  check(!input.pending());
  check(input.push("const char* s = \"})[\";", "test", 5).has_value());
  check(!input.push("/* comment", "test", 6));
  check(input.push(" */ int y = 1;", "test", 7).has_value());
  check(!input.push("auto raw = R\"tag({", "test", 8));
  check(input.push("})tag\";", "test", 9).has_value());
  check(input.push("int n = 1'000;", "test", 10).has_value());
  check(!input.push("int z = 1 +", "test", 11));
  check(input.push("2;", "test", 12).has_value());
  check(!input.push("#if 1", "test", 13));
  check(!input.push("int guarded = 4;", "test", 14));
  check(input.push("#endif", "test", 15).has_value());
  check(!input.push("#define ADD(x) \\", "test", 16));
  check(input.push("((x) + 1)", "test", 17).has_value());
  check(!input.push("Example object = factory()", "test", 18));
  check(input.push("  .with(42);", "test", 19).has_value());
  std::istringstream source(
      "#include <cstdio>\nint main(void) {\nint x = 1;\nif (x) {\n x++;\n}\nreturn 0;\n}\n");
  auto lines = crepl::read_source(source, true);
  check(lines.size() == 8 && lines[1].empty() && lines[6].empty() && lines[7].empty());
  check(lines[5] == "}");
  const char* args[] = {"crepl",     "-I",     "-I/tmp/includes", "-L",    "-L/tmp/libs",
                        "-lfixture", "--main", "example.cpp",     "--step"};
  auto options = crepl::parse_options(9, args);
  check(options.compiler.front() == "-I/tmp/includes");
  check(options.library_dirs.at(0) == "/tmp/libs" && options.libraries.at(0) == "fixture");
  check(options.extract_main && options.step && options.script == "example.cpp");
  bool failed = false;
  try {
    const char* invalid[] = {"crepl", "-I"};
    crepl::parse_options(2, invalid);
  } catch (const std::exception&) {
    failed = true;
  }
  check(failed);
  std::cout << "frontend checks passed\n";
}
