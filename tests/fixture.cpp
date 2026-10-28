/// @file
/// @brief External shared-library fixture; not linked into the REPL executable.
/// @brief Returns a predictable value to test -L/-l resolution.
extern "C" int crepl_fixture_answer() { return 42; }
