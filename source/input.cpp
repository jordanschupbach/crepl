/// @file
/// @brief Groups physical input into fragments suitable for incremental Clang.
#include <cctype>
#include <crepl/input.hpp>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace crepl {
std::string trim(const std::string& text) {
  const auto first = text.find_first_not_of(" \r\n\t");
  if (first == std::string::npos)
    return {};
  return text.substr(first, text.find_last_not_of(" \r\n\t") - first + 1);
}
LexicalState scan(const std::string& text) {
  LexicalState result;
  result.tokens.assign(text.size(), ' ');
  for (std::size_t i = 0; i < text.size();) {
    const char c = text[i];
    if (c == '\n')
      result.tokens[i] = '\n';
    if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
      const auto end = text.find('\n', i + 2);
      i = end == std::string::npos ? text.size() : end;
      continue;
    }
    if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
      const auto end = text.find("*/", i + 2);
      if (end == std::string::npos) {
        result.open = true;
        break;
      }
      i = end + 2;
      continue;
    }
    if (c == 'R' && i + 1 < text.size() && text[i + 1] == '"') {
      result.meaningful = true;
      const auto opening = text.find('(', i + 2);
      if (opening == std::string::npos || opening - i > 18) {
        result.open = true;
        break;
      }
      const auto end = text.find(")" + text.substr(i + 2, opening - i - 2) + "\"", opening + 1);
      if (end == std::string::npos) {
        result.open = true;
        break;
      }
      result.tokens[i] = 'v';
      i = end + (opening - i - 2) + 2;
      continue;
    }
    // A quote between digits is a C++ digit separator, not a character literal.
    const bool separator = c == '\'' && i > 0 && i + 1 < text.size() &&
                           std::isalnum(static_cast<unsigned char>(text[i - 1])) &&
                           std::isalnum(static_cast<unsigned char>(text[i + 1]));
    if (c == '"' || (c == '\'' && !separator)) {
      result.meaningful = true;
      result.tokens[i] = 'v';
      bool closed = false;
      for (++i; i < text.size(); ++i) {
        if (text[i] == '\\') {
          ++i;
          continue;
        }
        if (text[i] == c) {
          ++i;
          closed = true;
          break;
        }
      }
      if (!closed)
        result.open = true;
      continue;
    }
    result.tokens[i] = c;
    if (!std::isspace(static_cast<unsigned char>(c)))
      result.meaningful = true;
    switch (c) {
    case '{':
      ++result.braces;
      break;
    case '}':
      --result.braces;
      break;
    case '(':
      ++result.parentheses;
      break;
    case ')':
      --result.parentheses;
      break;
    case '[':
      ++result.brackets;
      break;
    case ']':
      --result.brackets;
      break;
    default:
      break;
    }
    ++i;
  }
  return result;
}
std::optional<Unit> Input::push(const std::string& line, const std::string& file,
                                std::size_t number) {
  if (pending_.code.empty()) {
    pending_.file = file;
    pending_.line = number;
  }
  pending_.code += line + '\n';
  auto state = scan(pending_.code);
  if (state.open || state.braces > 0 || state.parentheses > 0 || state.brackets > 0)
    return {};
  const auto last_line = trim(line);
  if (!last_line.empty() && last_line.back() == '\\')
    return {};
  int conditionals = 0;
  std::istringstream lines(state.tokens);
  std::string physical;
  while (std::getline(lines, physical)) {
    physical = trim(physical);
    if (std::regex_search(physical, std::regex(R"(^#\s*(if|ifdef|ifndef)\b)")))
      ++conditionals;
    if (std::regex_search(physical, std::regex(R"(^#\s*endif\b)")))
      --conditionals;
  }
  if (conditionals > 0)
    return {};
  auto tokens = trim(state.tokens);
  const bool directive = !tokens.empty() && tokens.front() == '#';
  if (!directive && !tokens.empty()) {
    const char last = tokens.back();
    // A declaration can continue with member calls on a following physical line.
    // Balanced parentheses alone do not terminate `Type name = factory()`.
    const std::regex declaration(
        R"(^\s*(?:(?:const|volatile|static|extern|inline|constexpr|constinit|thread_local|unsigned|signed|long|short)\s+)*(?:[A-Za-z_][\w:]*(?:\s*<[^;{}]*>)?)\s+[*&\s]*[A-Za-z_]\w*\s*(=|\{|\(|$))");
    std::smatch match;
    if (last != ';' && std::regex_search(tokens, match, declaration)) {
      const auto initializer = match[1].str();
      if (initializer != "(" || last != '}')
        return {};
    }
    if (std::string("=,+-*/|&?:.\\").find(last) != std::string::npos && !tokens.ends_with("++") &&
        !tokens.ends_with("--"))
      return {};
  }
  if (!state.meaningful) {
    clear();
    return {};
  }
  Unit complete = std::move(pending_);
  pending_ = {};
  return complete;
}
void Input::clear() { pending_ = {}; }
bool Input::pending() const { return !pending_.code.empty(); }
std::vector<std::string> read_source(std::istream& stream, bool extract_main) {
  std::vector<std::string> lines;
  for (std::string line; std::getline(stream, line);)
    lines.push_back(line);
  if (!extract_main)
    return lines;
  bool found = false, finished = false;
  std::string body;
  for (auto& line : lines) {
    if (!found) {
      if (std::regex_match(trim(line), std::regex(R"(int\s+main\s*\([^)]*\)\s*\{\s*(//.*)?)"))) {
        found = true;
        body = "{\n";
        line.clear();
      }
    } else if (!finished) {
      const auto before = scan(body);
      body += line + '\n';
      const auto after = scan(body);
      if (before.braces == 1 && !before.open &&
          std::regex_match(trim(line), std::regex(R"(return\s+0\s*;\s*(//.*)?)")))
        line.clear();
      if (after.braces == 0 && !after.open) {
        if (trim(scan(line).tokens) != "}")
          throw std::runtime_error("main closing brace must be on its own line");
        line.clear();
        finished = true;
      }
    }
  }
  if (!found || !finished)
    throw std::runtime_error(
        "--main requires a conventional int main(...) { wrapper and closing brace");
  return lines;
}
} // namespace crepl
