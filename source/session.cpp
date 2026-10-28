/// @file
/// @brief Embeds Clang's incremental compiler rather than replaying source.
#include <clang/AST/ASTContext.h>
#include <clang/AST/DeclCXX.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Interpreter/Interpreter.h>
#include <crepl/config.hpp>
#include <crepl/session.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/TargetSelect.h>
#include <map>
#include <mutex>
#include <regex>
#include <sstream>
#include <stdexcept>
namespace crepl {
/// @brief Escapes metadata delimiters; editor readers reverse these four escapes.
static std::string escaped(const std::string& text) {
  std::string out;
  for (char c : text) {
    switch (c) {
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      out += c;
    }
  }
  return out;
}
/// @brief Replaces one snapshot atomically, never exposing partial writes to an editor.
static void snapshot(const std::string& dir, const std::string& name, const std::string& text) {
  if (dir.empty())
    return;
  const auto path = std::filesystem::path(dir) / name;
  const auto temporary = path.string() + ".tmp";
  {
    std::ofstream out(temporary);
    out << text;
    if (!out)
      throw std::runtime_error("cannot write " + temporary);
  }
  std::filesystem::rename(temporary, path);
}
struct Session::Impl {
  std::string state_dir;        ///< Optional generic editor snapshot directory.
  std::string display_function; ///< Explicitly configured show-call adapter.
  std::string history;          ///< Escaped successful user fragments, one per line.
  std::map<std::string, clang::VarDecl*>
      variables; ///< Live AST declarations, excluding headers/helpers.
  std::unique_ptr<clang::Interpreter> interpreter; ///< Owned incremental compiler and executor.
};
/// @brief Converts an LLVM failure into an owned exception message.
static std::runtime_error failure(llvm::Error error) {
  return std::runtime_error(llvm::toString(std::move(error)));
}
Session::Session(const Options& options) : impl_(std::make_unique<Impl>()) {
  impl_->state_dir = options.state_dir;
  impl_->display_function = options.display_function;
  if (!impl_->display_function.empty() &&
      !std::regex_match(impl_->display_function, std::regex(R"([A-Za-z_]\w*(::[A-Za-z_]\w*)*)")))
    throw std::runtime_error("--display-function requires a qualified C++ identifier");
  if (!impl_->state_dir.empty())
    std::filesystem::create_directories(impl_->state_dir);
  static std::once_flag initialized; ///< Native target initialization is process-wide.
  std::call_once(initialized, [] {
    if (llvm::InitializeNativeTarget() || llvm::InitializeNativeTargetAsmPrinter() ||
        llvm::InitializeNativeTargetAsmParser())
      throw std::runtime_error("native LLVM target unavailable");
  });
  std::vector<std::string> flags = {"-resource-dir", config::resource_dir, "-Wno-invalid-constexpr",
                                    "-Wno-unknown-attributes"};
  const auto* includes = options.c ? config::c_includes : config::cpp_includes;
  for (const char* const* dir = includes; *dir; ++dir) {
    flags.emplace_back("-isystem");
    flags.emplace_back(*dir);
  }
  flags.insert(flags.end(), options.compiler.begin(), options.compiler.end());
  std::vector<const char*> args;
  for (const auto& flag : flags)
    args.push_back(flag.c_str());
  clang::IncrementalCompilerBuilder builder;
  builder.SetCompilerArgs(args);
  auto compiler = builder.CreateCpp();
  if (!compiler)
    throw failure(compiler.takeError());
  auto interpreter = clang::Interpreter::create(std::move(*compiler));
  if (!interpreter)
    throw failure(interpreter.takeError());
  impl_->interpreter = std::move(*interpreter);
  std::vector<std::string> dirs = options.library_dirs;
  for (const auto* name : {"LIBRARY_PATH", "LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH"}) {
    if (const auto* env = std::getenv(name)) {
      std::istringstream paths(env);
      for (std::string path; std::getline(paths, path, ':');)
        if (!path.empty())
          dirs.push_back(path);
    }
  }
  for (const char* const* dir = config::library_dirs; *dir; ++dir)
    dirs.emplace_back(*dir);
  for (const auto& library : options.libraries) {
    std::string path = library;
    if (library.find('/') == std::string::npos && !library.ends_with(".so") &&
        !library.ends_with(".dylib")) {
      path = "lib" + library +
#ifdef __APPLE__
             ".dylib";
#else
             ".so";
#endif
      for (const auto& dir : dirs) {
        const auto candidate = std::filesystem::path(dir) / path;
        if (std::filesystem::is_regular_file(candidate)) {
          path = candidate.string();
          break;
        }
      }
    }
    if (!load(path))
      throw std::runtime_error("could not load requested library " + library);
  }
}
Session::~Session() = default;
/// @def CREPL_READ(kind, type)
/// @brief Reads a matching host scalar kind from JIT storage without a method call.

/// @brief Reads an arithmetic/enum value without copying user objects or invoking methods.
static std::string scalar(clang::Interpreter& interpreter, const clang::VarDecl* variable) {
  if (!variable->getType()->isArithmeticType() && !variable->getType()->isEnumeralType())
    return {};
  auto address = interpreter.getSymbolAddress(clang::GlobalDecl(variable));
  if (!address) {
    llvm::consumeError(address.takeError());
    return "(not materialized)";
  }
  const void* ptr = address->toPtr<const void*>();
  auto type = variable->getType().getCanonicalType();
  if (type->isEnumeralType())
    type = type->getAs<clang::EnumType>()->getDecl()->getIntegerType();
  const auto* builtin = type->getAs<clang::BuiltinType>();
  if (!builtin)
    return {};
  std::ostringstream out;
  switch (builtin->getKind()) {
#define CREPL_READ(kind, type)                                                                     \
  case clang::BuiltinType::kind:                                                                   \
    out << +*static_cast<const type*>(ptr);                                                        \
    break
    CREPL_READ(Bool, bool);
    CREPL_READ(Char_S, char);
    CREPL_READ(Char_U, unsigned char);
    CREPL_READ(SChar, signed char);
    CREPL_READ(UChar, unsigned char);
    CREPL_READ(Short, short);
    CREPL_READ(UShort, unsigned short);
    CREPL_READ(Int, int);
    CREPL_READ(UInt, unsigned int);
    CREPL_READ(Long, long);
    CREPL_READ(ULong, unsigned long);
    CREPL_READ(LongLong, long long);
    CREPL_READ(ULongLong, unsigned long long);
    CREPL_READ(Float, float);
    CREPL_READ(Double, double);
    CREPL_READ(LongDouble, long double);
#undef CREPL_READ
  default:
    break;
  }
  return out.str();
}
void Session::refresh() {
  auto& ast = impl_->interpreter->getASTContext();
  auto& sources = ast.getSourceManager();
  impl_->variables.clear();
  std::map<std::string, std::string> definitions;
  /// @brief Indexes user declarations across incremental translation units, excluding header
  /// definitions.
  std::function<void(clang::DeclContext*)> visit = [&](clang::DeclContext* context) {
    for (auto* decl : context->decls()) {
      if (decl->isImplicit() || decl->isInvalidDecl() || decl->getLocation().isInvalid())
        continue;
      const auto location = sources.getSpellingLoc(decl->getLocation());
      const auto buffer = sources.getBufferName(location);
      const auto presumed = sources.getPresumedLoc(location);
      if (!buffer.starts_with("input_line_") || !presumed.isValid() ||
          std::string(presumed.getFilename()).starts_with("<crepl-"))
        continue;
      if (auto* ns = llvm::dyn_cast<clang::NamespaceDecl>(decl)) {
        visit(ns);
        continue;
      }
      auto* named = llvm::dyn_cast<clang::NamedDecl>(decl);
      if (!named || !named->getIdentifier())
        continue;
      auto name = named->getQualifiedNameAsString();
      if (name.starts_with("__") || name.starts_with("crepl_detail::"))
        continue;
      if (auto* variable = llvm::dyn_cast<clang::VarDecl>(decl)) {
        if (!variable->hasGlobalStorage() ||
            variable->isThisDeclarationADefinition() == clang::VarDecl::DeclarationOnly)
          continue;
        impl_->variables[name] = variable;
      } else if (llvm::isa<clang::FunctionDecl, clang::RecordDecl, clang::TypedefNameDecl,
                           clang::EnumDecl>(decl)) {
        definitions[name] = std::string(decl->getDeclKindName()) + " " + name;
      }
    }
  };
  for (auto* tu = ast.getTranslationUnitDecl()->getMostRecentDecl(); tu; tu = tu->getPreviousDecl())
    visit(tu);
  std::string objects, defs;
  for (const auto& [name, variable] : impl_->variables) {
    objects += escaped(name) + "\t" + escaped(variable->getType().getAsString()) + "\t" +
               escaped(scalar(*impl_->interpreter, variable)) + "\n";
  }
  for (const auto& [name, text] : definitions)
    defs += text + "\n";
  snapshot(impl_->state_dir, "objects.tsv", objects);
  snapshot(impl_->state_dir, "definitions.txt", defs);
  snapshot(impl_->state_dir, "history.txt", impl_->history);
}
bool Session::inspect(const std::string& name) {
  refresh();
  auto found = impl_->variables.find(name);
  if (found == impl_->variables.end()) {
    std::cerr << "crepl: no live variable named " << name << '\n';
    return false;
  }
  auto* variable = found->second;
  auto& ast = impl_->interpreter->getASTContext();
  std::string text = name + " : " + variable->getType().getAsString() + "\n";
  const auto value = scalar(*impl_->interpreter, variable);
  if (!value.empty())
    text += "value: " + value + "\n";
  auto type = variable->getType().getNonReferenceType();
  if (const auto* record = type->getAsCXXRecordDecl()) {
    text += "\nMembers (declared by the compiler):\n";
    unsigned count = 0;
    for (const auto* member : record->decls()) {
      const auto* named = llvm::dyn_cast<clang::NamedDecl>(member);
      if (!named || member->isImplicit() || !named->getIdentifier())
        continue;
      if (++count > 100) {
        text += "...\n";
        break;
      }
      text += member->getAccess() == clang::AS_private     ? "private "
              : member->getAccess() == clang::AS_protected ? "protected "
                                                           : "public ";
      text += named->getNameAsString();
      if (const auto* value_decl = llvm::dyn_cast<clang::ValueDecl>(member))
        text += " : " + value_decl->getType().getAsString();
      text += "\n";
    }
  }
  const auto location = ast.getSourceManager().getPresumedLoc(variable->getLocation());
  if (location.isValid())
    text += "\ndeclared at " + std::string(location.getFilename()) + ":" +
            std::to_string(location.getLine()) + "\n";
  snapshot(impl_->state_dir, "inspect.txt", text);
  std::cout << text;
  return true;
}
/// @def X(type, name)
/// @brief Emits the matching scalar getter without narrowing its numeric type.

/// @brief Prints scalar results; complex values show their type and storage address.
/// LLVM 21's Value::dump is a placeholder, so render the public Value API here.
static void print_value(const clang::Value& value) {
  if (!value.hasValue())
    return;
  std::cout << '(' << value.getType().getAsString() << ") ";
  switch (value.getKind()) {
#define X(type, name)                                                                              \
  case clang::Value::K_##name:                                                                     \
    std::cout << +value.get##name();                                                               \
    break;
    REPL_BUILTIN_TYPES
#undef X
  case clang::Value::K_PtrOrObj:
    std::cout << value.getPtr();
    break;
  default:
    break;
  }
  std::cout << '\n';
}
bool Session::execute(const Unit& unit) {
  // #line preserves meaningful file/line diagnostics across incremental inputs.
  std::string file;
  for (char c : unit.file) {
    if (c == '\\' || c == '"')
      file += '\\';
    file += c;
  }
  std::string code = unit.code;
  if (!impl_->display_function.empty()) {
    // Opt-in and deliberately narrow: standalone receiver.show(); statements only.
    const std::regex show(R"(^\s*([A-Za-z_]\w*(?:::\w+|\.\w+)*)\.show\s*\(\s*\)\s*;?\s*$)");
    std::istringstream lines(code);
    std::string line, transformed, prefix;
    while (std::getline(lines, line)) {
      std::smatch match;
      transformed += !scan(prefix).open && std::regex_match(line, match, show)
                         ? impl_->display_function + "(" + match[1].str() + ");\n"
                         : line + "\n";
      prefix += line + '\n';
    }
    code = transformed;
  }
  const auto source = "#line " + std::to_string(unit.line) + " \"" + file + "\"\n" + code;
  try {
    clang::Value value;
    if (auto error = impl_->interpreter->ParseAndExecute(source, &value)) {
      std::cerr << "crepl: " << llvm::toString(std::move(error)) << '\n';
      snapshot(impl_->state_dir, "status.txt", "error\n");
      return false;
    }
    print_value(value);
  } catch (const std::exception& error) {
    snapshot(impl_->state_dir, "status.txt", "runtime error\n");
    std::cerr << "crepl: runtime exception: " << error.what() << '\n';
    return false;
  } catch (...) {
    std::cerr << "crepl: unknown runtime exception\n";
    return false;
  }
  if (!impl_->state_dir.empty()) {
    impl_->history += escaped(unit.code) + "\n";
    refresh();
    snapshot(impl_->state_dir, "status.txt", "ok\n");
  }
  std::cout.flush();
  std::cerr.flush();
  std::fflush(nullptr);
  return true;
}
bool Session::load(const std::string& path) {
  if (auto error = impl_->interpreter->LoadDynamicLibrary(path.c_str())) {
    std::cerr << "crepl: " << llvm::toString(std::move(error)) << '\n';
    return false;
  }
  return true;
}
} // namespace crepl
