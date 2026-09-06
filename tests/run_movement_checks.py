"""Build portable helper tests with GCC/Clang. This does not build or execute the CS2 module."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
compiler = shutil.which('c++') or shutil.which('g++') or shutil.which('clang++')
if not compiler:
    raise SystemExit('A C++20 GCC/Clang compiler is required for the portable C++ tests.')
subprocess.run([sys.executable, str(root / 'tests/test_movement_model.py')], cwd=root, check=True)
with tempfile.TemporaryDirectory(prefix='nemesis-checks-') as temporary:
    for name, sources, defines in (
        ('movement_math_tests', ['tests/movement_math_tests.cpp'], []),
        ('command_regression_tests', ['tests/command_regression_tests.cpp', 'utilities/proto/proto.cpp'], ['-DNEMESIS_PROTO_STANDALONE']),
    ):
        executable = Path(temporary) / name
        subprocess.run([compiler, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic',
                        *defines, *sources, '-o', str(executable)], cwd=root, check=True)
        subprocess.run([str(executable)], cwd=root, check=True)
print('Portable movement checks: PASS. The Windows module and live game were not tested.')
