"""Executable/JIT contracts; no client library or third-party Python dependency."""
from pathlib import Path
import os
import pty
import select
import subprocess
import sys
import tempfile
import time
import unittest

EXE, LIBRARY = sys.argv[1:3]
del sys.argv[1:3]

class Integration(unittest.TestCase):
    def run_code(self, code, *flags, success=True):
        p = subprocess.run([EXE, *flags], input=code, text=True,
                           capture_output=True, timeout=45)
        self.assertEqual(p.returncode == 0, success, (flags, p.returncode, p.stdout, p.stderr))
        return p

    def test_persistent_variables(self):
        p = self.run_code('int x = 40;\nx += 2;\nx\n')
        self.assertEqual(p.stdout, '(int) 42\n')

    def test_function_multiline(self):
        p = self.run_code('int square(int x)\n{\nreturn x*x;\n}\nsquare(6) + 6\n')
        self.assertEqual(p.stdout, '(int) 42\n')

    def test_side_effects_execute_once(self):
        p = self.run_code('#include <cstdio>\nint counter = 0;\nprintf("count=%d\\n", ++counter);\nprintf("count=%d\\n", ++counter);\n')
        self.assertEqual(p.stdout, 'count=1\ncount=2\n')

    def test_vector_state(self):
        p = self.run_code('#include <vector>\n#include <cstdio>\nstd::vector<int> v = {1,\n2,3};\nv.push_back(4);\nprintf("size=%zu\\n", v.size());\n')
        self.assertEqual(p.stdout, 'size=4\n')

    def test_chained_initializer(self):
        p = self.run_code('struct Builder { int n; Builder with(int x) const { return {n+x}; } };\nBuilder value = Builder{1}\n .with(41);\nvalue.n\n')
        self.assertEqual(p.stdout, '(int) 42\n')

    def test_standard_print(self):
        p = self.run_code('#include <print>\nstd::print("hello {}\\n", 42);\n')
        self.assertEqual(p.stdout, 'hello 42\n')

    def test_c17(self):
        p = self.run_code('#include <stdio.h>\nint n = 40;\nn += 2;\nprintf("%d\\n", n);\n', '-x', 'c')
        self.assertEqual(p.stdout, '42\n')

    def test_library_flags(self):
        p = self.run_code('extern "C" int crepl_fixture_answer();\ncrepl_fixture_answer()\n',
                          '-L', '-L' + str(Path(LIBRARY).parent), '-lcrepl_fixture')
        self.assertEqual(p.stdout, '(int) 42\n')

    def test_explicit_library(self):
        p = self.run_code('extern "C" int crepl_fixture_answer();\ncrepl_fixture_answer()\n', '--load', LIBRARY)
        self.assertEqual(p.stdout, '(int) 42\n')

    def test_batch_stops_after_error(self):
        p = self.run_code('#include <cstdio>\nint bad = ;\nputs("must not run");\n', success=False)
        self.assertNotIn('must not run', p.stdout)
        self.assertIn('error:', p.stderr)

    def test_incomplete_input(self):
        p = self.run_code('int incomplete = (\n', success=False)
        self.assertIn('incomplete fragment', p.stderr)

    def test_missing_library(self):
        self.run_code('', '-lcrepl_does_not_exist', success=False)

    def test_explicit_block(self):
        p = self.run_code(':begin\nint twice(int x)\n{ return x*2; }\n:end\ntwice(21)\n')
        self.assertEqual(p.stdout, '(int) 42\n')

    def test_cancel(self):
        p = self.run_code('int x = (\n:cancel\nint x = 42;\nx\n')
        self.assertEqual(p.stdout, '(int) 42\n')

    def test_script_and_step_state(self):
        with tempfile.TemporaryDirectory(prefix='crepl-test-') as directory:
            source = Path(directory) / 'example.cpp'
            source.write_text('#include <cstdio>\nint main(void) {\nint x = 40;\nx += 2;\nprintf("answer=%d\\n", x);\nreturn 0;\n}\n')
            self.assertEqual(self.run_code('', '--main', str(source)).stdout, 'answer=42\n')
            p = self.run_code(':next\n:next\nx\n:next\nx\n:next\n', '--main', str(source), '--step')
            self.assertEqual(p.stdout, '(int) 40\n(int) 42\nanswer=42\n')
            script = Path(directory) / 'script with spaces.cpp'
            script.write_text('int value = 42;\n')
            self.assertEqual(self.run_code(f':load {script}\nvalue\n').stdout, '(int) 42\n')
            source.write_text('int broken = ;\n')
            p = self.run_code('', '--script', str(source), success=False)
            self.assertIn(str(source) + ':1:', p.stderr)

    def test_normal_destruction(self):
        p = self.run_code('#include <cstdio>\nstruct Loud { ~Loud() { puts("destroyed"); } };\nLoud value;\n')
        self.assertEqual(p.stdout, 'destroyed\n')

    def test_runtime_exception(self):
        p = self.run_code('#include <stdexcept>\nthrow std::runtime_error("test failure");\n', success=False)
        self.assertIn('runtime exception: test failure', p.stderr)

    def test_macros_and_raw_strings(self):
        p = self.run_code('#include <cstdio>\n#define ADD(x) \\\n ((x) + 1)\n#if 1\nint n = ADD(41);\n#endif\nprintf("%d\\n", n);\nconst char* raw = R"tag({\n})tag";\nprintf("%s\\n", raw);\n')
        self.assertEqual(p.stdout, '42\n{\n}\n')

    def test_compiler_flags(self):
        with tempfile.TemporaryDirectory(prefix='crepl-header-') as directory:
            (Path(directory) / 'answer.hpp').write_text('constexpr int answer = VALUE;\n')
            p = self.run_code('#include <answer.hpp>\nanswer\n', '-I', '-I' + directory, '-DVALUE=42')
            self.assertEqual(p.stdout, '(const int) 42\n')

    def test_physical_lines_execute_before_eof(self):
        with subprocess.Popen([EXE], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, text=True, bufsize=1) as p:
            for code, expected in [('#include <cstdio>\nint n = 40;\nprintf("first=%d\\n", n);\n', 'first=40\n'),
                                   ('n += 2;\nprintf("next=%d\\n", n);\n', 'next=42\n')]:
                for line in code.splitlines():
                    p.stdin.write(line + '\n')
                    p.stdin.flush()
                ready, _, _ = select.select([p.stdout], [], [], 15)
                self.assertTrue(ready, 'JIT did not execute before EOF')
                self.assertEqual(p.stdout.readline(), expected)
            p.stdin.close()
            self.assertEqual(p.wait(timeout=15), 0)
            self.assertEqual(p.stderr.read(), '')

    def test_terminal_recovers_after_parse_error(self):
        # A real TTY distinguishes interactive recovery from fail-fast batch input.
        master, slave = pty.openpty()
        p = subprocess.Popen([EXE], stdin=slave, stdout=slave, stderr=slave, close_fds=True)
        os.close(slave)
        output = bytearray()
        try:
            os.write(master, b'int stable = 42;\nint bad = ;\nstable\n:quit\n')
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline:
                if select.select([master], [], [], 0.1)[0]:
                    try:
                        chunk = os.read(master, 65536)
                    except OSError:
                        break
                    if not chunk:
                        break
                    output.extend(chunk)
                elif p.poll() is not None:
                    break
            self.assertEqual(p.wait(timeout=3), 0, output.decode(errors='replace'))
            self.assertIn(b'error:', output)
            self.assertIn(b'(int) 42', output)
        finally:
            if p.poll() is None:
                p.kill()
                p.wait()
            os.close(master)


    def test_editor_snapshots_and_inspection(self):
        with tempfile.TemporaryDirectory(prefix='crepl-editor-') as directory:
            p = self.run_code('#include <vector>\nint counter = 40;\ncounter += 2;\nstruct Item { int field; void method() {} };\nItem item{7};\nstd::vector<int> data{1,2};\n:inspect item\n', '--state-dir', directory)
            root = Path(directory)
            objects = (root / 'objects.tsv').read_text()
            self.assertIn('counter\tint\t42\n', objects)
            self.assertIn('data\tstd::vector<int>\t\n', objects)
            self.assertEqual(len(objects.splitlines()), 3)
            detail = (root / 'inspect.txt').read_text()
            self.assertIn('public field : int', detail)
            self.assertIn('public method : void', detail)
            self.assertIn('declared at <stdin>:', detail)
            self.assertIn('CXXRecord Item', (root / 'definitions.txt').read_text())
            self.assertEqual((root / 'status.txt').read_text(), 'ok\n')
            self.assertFalse(list(root.glob('*.tmp')))
            self.assertEqual(len((root / 'history.txt').read_text().splitlines()), 6)

    def test_editor_evalat_and_main_loading(self):
        with tempfile.TemporaryDirectory(prefix='crepl-cursor-') as directory:
            path = Path(directory) / 'source with spaces.cpp'
            path.write_text('int main(void) {\nint first = 40;\nint second = (\nfirst + 2);\nreturn 0;\n}\n')
            p = self.run_code(f':evalat 2 {path}\n:evalat 4 {path}\nsecond\n')
            self.assertEqual(p.stdout, '(int) 42\n')
            p = self.run_code(f':load-auto {path}\nsecond\n')
            self.assertEqual(p.stdout, '(int) 42\n')

    def test_configurable_display_adapter(self):
        code = ('#include <cstdio>\nstruct Picture { void show() { puts("native"); } };\n'
                'void capture(Picture&) { puts("captured"); }\nPicture picture;\npicture.show();\n')
        self.assertEqual(self.run_code(code).stdout, 'native\n')
        self.assertEqual(self.run_code(code, '--display-function', 'capture').stdout, 'captured\n')
        raw = code + 'const char* source = R"raw(\npicture.show();\n)raw";\nprintf("%s", source);\n'
        self.assertIn('picture.show();', self.run_code(raw, '--display-function', 'capture').stdout)

if __name__ == '__main__':
    unittest.main(verbosity=2)
