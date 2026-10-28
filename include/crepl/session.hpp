/// @file
/// @brief Incremental JIT session ownership and dynamic-library loading.
#pragma once
#include <crepl/input.hpp>
#include <crepl/options.hpp>
#include <memory>
#include <string>
namespace crepl {
/// @brief Owns one Clang incremental parser and LLVM JIT; no previous code is replayed.
/// Not thread safe. Objects persist across successful inputs and their destructors
/// run on normal session teardown. Code executes with process permissions.
class Session {
public:
  /// @brief Initializes Clang and loads libraries; throws on configuration errors.
  explicit Session(const Options& options);
  /// @brief Destroys the JIT and runs registered object destructors.
  ~Session();
  Session(const Session&) = delete;            ///< Sessions cannot share mutable interpreter state.
  Session& operator=(const Session&) = delete; ///< Sessions cannot be copied.
  /// @brief Parses and executes one complete fragment; reports errors and returns false.
  /// Parse errors leave earlier inputs available. Runtime/JIT failures may require
  /// a new session; arbitrary side effects cannot be rolled back.
  bool execute(const Unit& unit);
  /// @brief Loads a shared object by explicit path or platform library name.
  bool load(const std::string& path);
  /// @brief Writes type, location, record members and scalar values for a named variable.
  bool inspect(const std::string& name);
  /// @brief Atomically refreshes optional editor metadata without replaying user code.
  void refresh();

private:
  struct Impl;                 ///< LLVM-dependent implementation hidden from frontend headers.
  std::unique_ptr<Impl> impl_; ///< Exclusive parser/JIT state.
};
} // namespace crepl
