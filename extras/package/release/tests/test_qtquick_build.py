# SPDX-License-Identifier: GPL-2.0-or-later
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest

SCRIPTS = Path(__file__).resolve().parents[1]


class QtQuickBuildTests(unittest.TestCase):
    def run_build(self, *, version='6.8.3', corrupt=False, fail_probe=False, mismatch_features=False):
        with tempfile.TemporaryDirectory(prefix='Qt Quick release ') as temp:
            root = Path(temp)
            scripts = root / 'extras/package/release'
            scripts.mkdir(parents=True)
            shutil.copyfile(SCRIPTS / 'build-qtquick.sh', scripts / 'build-qtquick.sh')
            patches = root / 'contrib/src/qtdeclarative'
            patches.mkdir(parents=True)
            (patches / '0001-QtQuick-parent-pixmap-cache-timer-to-application.patch').touch()
            archive = root / 'source.tar.xz'
            with tarfile.open(archive, 'w:xz') as stream:
                source = b'fixture\n'
                info = tarfile.TarInfo('qtdeclarative-everywhere-src-6.8.3/CMakeLists.txt')
                info.size = len(source)
                stream.addfile(info, io.BytesIO(source))
            digest = '0' * 128 if corrupt else hashlib.sha512(archive.read_bytes()).hexdigest()
            (patches / 'SHA512SUMS').write_text(
                f'{digest} qtdeclarative-everywhere-src-6.8.3.tar.xz\n')
            sdk = root / 'Qt SDK'
            (sdk / 'bin').mkdir(parents=True)
            (sdk / 'lib').mkdir()
            for module in ('Qml', 'Quick'):
                include = sdk / 'include' / ('Qt' + module)
                private = include / '6.8.3' / ('Qt' + module) / 'private'
                private.mkdir(parents=True)
                (include / ('qt' + module.lower() + '-config.h')).write_text('features\n')
                (private / ('qt' + module.lower() + '-config_p.h')).write_text('features\n')
            library = sdk / 'lib/libQt6Quick.so.6.8.3'
            library.write_text('original')
            qmake = sdk / 'bin/qmake'
            qmake.write_text('#!/bin/sh\nprintf "%s\\n" "$QT_TEST_VERSION"\n')
            qmake.chmod(0o755)
            commands = root / 'commands'
            commands.mkdir()
            log = root / 'commands.jsonl'
            record = '''import json, os, sys
from pathlib import Path
with Path(os.environ['QT_TEST_LOG']).open('a') as stream:
    stream.write(json.dumps([Path(sys.argv[0]).name, *sys.argv[1:]]) + '\\n')
'''
            fixtures = {
                'curl': record + '''import shutil
shutil.copyfile(os.environ['QT_TEST_ARCHIVE'], sys.argv[sys.argv.index('-o') + 1])
''',
                'patch': record,
                'patchelf': record,
                'cmake': record + '''
if '-B' in sys.argv and sys.argv[sys.argv.index('-B') + 1].endswith('/build'):
    build = Path(sys.argv[sys.argv.index('-B') + 1])
    for module in ('qml', 'quick'):
        output = build / 'src' / module
        output.mkdir(parents=True)
        for suffix in ('-config.h', '-config_p.h'):
            value = 'different features\\n' if os.environ['QT_TEST_MISMATCH_FEATURES'] == '1' else 'features\\n'
            (output / ('qt' + module + suffix)).write_text(value)
if '--build' in sys.argv:
    build = Path(sys.argv[sys.argv.index('--build') + 1])
    build.mkdir(parents=True, exist_ok=True)
    if '--target' in sys.argv:
        (build / 'lib').mkdir()
        (build / 'lib/libQt6Quick.so.6.8.3').write_text('patched')
    else:
        probe = build / 'cache-lifetime'
        probe.write_text('#!/usr/bin/env python3\\n' + ''' + repr(record) + ''' +
                         "raise SystemExit(23 if os.environ['QT_TEST_FAIL_PROBE'] == '1' else 0)\\n")
        probe.chmod(0o755)
''',
            }
            for name, body in fixtures.items():
                command = commands / name
                command.write_text('#!/usr/bin/env python3\n' + body)
                command.chmod(0o755)
            env = dict(os.environ, PATH=str(commands) + os.pathsep + os.environ['PATH'],
                       QT_ROOT_DIR=str(sdk), QT_TEST_VERSION=version,
                       QT_TEST_ARCHIVE=str(archive), QT_TEST_LOG=str(log),
                       QT_TEST_FAIL_PROBE=str(int(fail_probe)),
                       QT_TEST_MISMATCH_FEATURES=str(int(mismatch_features)))
            result = subprocess.run(['bash', str(scripts / 'build-qtquick.sh')],
                                    env=env, text=True, capture_output=True, timeout=20)
            calls = [json.loads(line) for line in log.read_text().splitlines()] if log.exists() else []
            return result, calls, library.read_text()

    def test_installs_only_after_all_lifetime_regressions_pass(self):
        result, calls, library = self.run_build()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(library, 'patched')
        self.assertEqual([call[1:] for call in calls if call[0] == 'cache-lifetime'], [
            ['same-thread', '1', 'window'], ['same-thread', '2', 'window'],
            ['different-threads', '2', 'window'], ['same-thread', '1', 'no-window'],
            ['same-thread', '2', 'no-window'], ['different-threads', '2', 'no-window'],
            ['same-thread', '1', 'app-owned'], ['same-thread', '2', 'app-owned'],
            ['different-threads', '2', 'app-owned'],
        ])

    def test_regression_failure_preserves_the_installed_sdk(self):
        result, calls, library = self.run_build(fail_probe=True)
        self.assertEqual(result.returncode, 23, result.stderr)
        self.assertEqual(library, 'original')
        self.assertEqual(len([call for call in calls if call[0] == 'cache-lifetime']), 1)

    def test_checksum_failure_prevents_build_and_install(self):
        result, calls, library = self.run_build(corrupt=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(library, 'original')
        self.assertFalse(any(call[0] in ('patch', 'cmake') for call in calls))

    def test_unverified_sdk_version_is_rejected_before_download(self):
        result, calls, library = self.run_build(version='6.9.3')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('No verified Qt Declarative source', result.stderr)
        self.assertEqual(calls, [])
        self.assertEqual(library, 'original')

    def test_different_sdk_features_prevent_build_and_install(self):
        result, calls, library = self.run_build(mismatch_features=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(library, 'original')
        self.assertFalse(any(call[:2] == ['cmake', '--build'] for call in calls))


if __name__ == '__main__':
    unittest.main()
