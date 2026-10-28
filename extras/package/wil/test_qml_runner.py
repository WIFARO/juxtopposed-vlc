#!/usr/bin/env python
"""Behavioral integration tests for the standalone QML test runner's exit code."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

RUNNER = Path(__file__).with_name('run_qml_tests.py')


class RunnerExitCode(unittest.TestCase):
    def run_fixture(self, expected):
        with tempfile.TemporaryDirectory(prefix='wil-qml-runner-', dir=os.environ['TMPDIR']) as directory:
            root = Path(directory)
            qml = root / 'tst_Exit.qml'
            qml.write_text('import QtQuick\nimport QtTest\nTestCase { name: "ExitProbe"; '
                           'function test_result() { compare(1, ' + str(expected) + ') } }\n',
                           encoding='utf-8')
            log = root / 'result.log'
            result = subprocess.run([sys.executable, str(RUNNER), '-input', str(qml),
                                     '-o', str(log) + ',txt'],
                                    capture_output=True, text=True, timeout=30)
            return result.returncode, log.read_text(encoding='utf-8')

    def test_failed_qml_assertion_returns_nonzero(self):
        code, log = self.run_fixture(2)
        self.assertIn('ExitProbe::test_result()', log)
        self.assertIn('FAIL!', log)
        self.assertNotEqual(code, 0, 'A real failed QML assertion must fail the runner process')

    def test_successful_qml_assertion_returns_zero(self):
        code, log = self.run_fixture(1)
        self.assertIn('ExitProbe::test_result()', log)
        self.assertNotIn('FAIL!', log)
        self.assertEqual(code, 0)


if __name__ == '__main__':
    unittest.main(verbosity=2)
