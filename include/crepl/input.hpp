/// @file
/// @brief Lexical input grouping and optional conventional-main extraction.
#pragma once
#include <cstddef>
#include <istream>
#include <optional>
#include <string>
#include <vector>

/// @brief Independent C/C++ incremental interpreter frontend.
namespace crepl {
/// @brief Source text with its original file and starting physical line.
struct Unit {
  std::string code;     ///< Complete source fragment, retaining newlines.
  std::string file;     ///< Diagnostic filename, or the standard-input label.
  std::size_t line = 1; ///< One-based source line.
};
/// @brief Accumulates multiline statements without executing incomplete fragments.
/// Balances delimiters, comments, ordinary/raw strings, preprocessor conditionals,
/// and explicit trailing backslashes. Clang performs syntax/type checking.
class Input {
public:
  /// @brief Adds one physical line; returns a complete unit when available.
  std::optional<Unit> push(const std::string& line, const std::string& file, std::size_t number);
  /// @brief Discards pending text; does not mutate interpreter state.
  void clear();
  /// @brief Reports whether a fragment still needs more input.
  bool pending() const;

private:
  Unit pending_; ///< Pending text and its original source location.
};
/// @brief Reports lexical delimiter depth, excluding comments and literals.
struct LexicalState {
  int braces = 0;          ///< Curly-brace nesting depth.
  int parentheses = 0;     ///< Parenthesis nesting depth.
  int brackets = 0;        ///< Square-bracket nesting depth.
  bool open = false;       ///< Unterminated comment or literal.
  bool meaningful = false; ///< Contains non-comment source tokens.
  std::string tokens;      ///< Source with comments/literals masked, for grouping only.
};
/// @brief Scans source for grouping; never attempts to parse C++ grammar.
LexicalState scan(const std::string& text);
/// @brief Reads physical lines, optionally removing a conventional int main wrapper.
/// With extract_main, supports int main(...) { on one line and its final return 0;
/// on a separate line. Preserves all line numbers and all other code. Rejects
/// missing/unclosed wrappers; this is a convenience, not a C++ source transformer.
std::vector<std::string> read_source(std::istream& stream, bool extract_main);
/// @brief Removes ASCII whitespace at both ends.
std::string trim(const std::string& text);
} // namespace crepl
