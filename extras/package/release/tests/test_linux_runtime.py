# SPDX-License-Identifier: GPL-2.0-or-later
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

SCRIPTS = Path(__file__).resolve().parents[1]
def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, SCRIPTS / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module
runtime = load('linux_runtime', 'bundle-linux-runtime.py')
tools = load('linux_tools', 'fetch-linux-tools.py')


class RuntimeTests(unittest.TestCase):
    def test_bundled_compiler_runtime_is_rejected_before_graphics_testing(self):
        with tempfile.TemporaryDirectory() as temp:
            app = Path(temp)
            for name in runtime.REQUIRED_PLUGINS:
                path = app / 'usr/plugins' / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
            (app / 'usr/lib').mkdir()
            (app / 'usr/lib/libstdc++.so.6').write_bytes(b'old compiler runtime')
            with self.assertRaisesRegex(ValueError, 'Bundled host ABI library'):
                runtime.audit(app)

    def test_platform_plugins_alone_do_not_prove_wayland_egl_is_bundled(self):
        with tempfile.TemporaryDirectory() as temp:
            app = Path(temp)
            for name in runtime.REQUIRED_PLUGINS:
                if 'graphics-integration' in name:
                    continue
                path = app / 'usr/plugins' / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
            with self.assertRaisesRegex(ValueError, 'wayland-graphics-integration-client'):
                runtime.verify_plugins(app)

    def test_launcher_relocates_and_does_not_leak_library_or_qt_overrides(self):
        with tempfile.TemporaryDirectory() as temp:
            app = Path(temp) / 'VLC [Juxtopposed]' / 'AppDir'
            (app / 'usr/bin').mkdir(parents=True)
            launch = app / 'AppRun'
            shutil.copy2(SCRIPTS / 'AppRun.linux', launch)
            launch.chmod(0o755)
            binary = app / 'usr/bin/vlc'
            binary.write_text(f'#!{sys.executable}\nimport os,sys,json\nprint(json.dumps(dict(cwd=os.getcwd(), args=sys.argv[1:], env=dict(os.environ))))\n')
            binary.chmod(0o755)
            env = dict(os.environ, APPDIR='/wrong/app', LD_LIBRARY_PATH='/wrong/lib',
                       QT_PLUGIN_PATH='/wrong/qt', QT_QPA_PLATFORM_PLUGIN_PATH='/wrong/platforms',
                       QML_IMPORT_PATH='/wrong/qml', QML2_IMPORT_PATH='/wrong/qml2')
            result = json.loads(subprocess.check_output([str(launch), 'relative file.mkv'], env=env, cwd=temp, text=True))
            self.assertEqual(result['cwd'], temp)
            self.assertEqual(result['args'], ['-I', 'qt', 'relative file.mkv'])
            self.assertEqual(result['env']['VLC_PLUGIN_PATH'], str(app / 'usr/lib/vlc/plugins'))
            for name in ('LD_LIBRARY_PATH', 'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QML_IMPORT_PATH', 'QML2_IMPORT_PATH'):
                self.assertNotIn(name, result['env'])

    @unittest.skipUnless(sys.platform.startswith('linux') and shutil.which('gcc') and shutil.which('patchelf'),
                         'needs native ELF compiler and patchelf (Linux build image provides both)')
    def test_relative_runpath_resolves_nested_plugins_after_move_with_spaces(self):
        with tempfile.TemporaryDirectory() as temp:
            app = Path(temp) / 'build'
            lib = app / 'usr/lib'
            plugin = lib / 'vlc/plugins/test'
            plugin.mkdir(parents=True)
            source = Path(temp) / 'dependency.c'
            source.write_text('int value(void) { return 42; }\n')
            subprocess.run(['gcc', '-shared', '-fPIC', str(source), '-Wl,-soname,libvalue.so', '-o', str(lib / 'libvalue.so')], check=True)
            source.write_text('extern int value(void); int main(void) { return value() == 42 ? 0 : 1; }\n')
            binary = plugin / 'consumer'
            subprocess.run(['gcc', str(source), '-L'+str(lib), '-lvalue', '-Wl,-rpath,/removed/build/path', '-o', str(binary)], check=True)
            runtime.set_runpath(binary, lib)
            moved = Path(temp) / 'moved with spaces'
            app.rename(moved)
            clean = dict(os.environ)
            clean.pop('LD_LIBRARY_PATH', None)
            subprocess.run([str(moved / binary.relative_to(app))], env=clean, check=True)

    def test_dependency_parser_accepts_relocated_paths_with_spaces(self):
        with mock.patch.object(runtime, 'run', return_value=' libvalue.so => /tmp/VLC [Juxtopposed]/AppDir/usr/lib/libvalue.so (0x123)\n'):
            self.assertEqual(runtime.dependencies(Path('/app'), {}), [('libvalue.so', '/tmp/VLC [Juxtopposed]/AppDir/usr/lib/libvalue.so')])
        with mock.patch.object(runtime, 'run', return_value=' libvalue.so => not found\n'):
            with self.assertRaisesRegex(ValueError, 'Unresolved dependencies'):
                runtime.dependencies(Path('/app'), {})


class ToolTests(unittest.TestCase):
    def test_modified_cached_tool_is_rejected_without_execution_or_download(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'linuxdeploy-x86_64.AppImage'
            path.write_bytes(b'wrong cache')
            with mock.patch.object(tools, 'TOOLS', {'linuxdeploy': '0'*64}), mock.patch.object(tools.urllib.request, 'urlopen') as fetch:
                with self.assertRaisesRegex(ValueError, 'Cached deployment tool checksum mismatch'):
                    tools.fetch(Path(temp))
                fetch.assert_not_called()

    def test_bad_download_is_never_promoted_to_the_executable_cache(self):
        with tempfile.TemporaryDirectory() as temp:
            with mock.patch.object(tools, 'TOOLS', {'linuxdeploy': '0'*64}), \
                 mock.patch.object(tools.urllib.request, 'urlopen', return_value=io.BytesIO(b'bad download')):
                with self.assertRaisesRegex(ValueError, 'Deployment tool checksum mismatch'):
                    tools.fetch(Path(temp))
            self.assertEqual(list(Path(temp).iterdir()), [])

    def test_verified_download_is_cached(self):
        with tempfile.TemporaryDirectory() as temp:
            payload = b'verified tool'
            with mock.patch.object(tools, 'TOOLS', {'linuxdeploy': hashlib.sha256(payload).hexdigest()}), \
                 mock.patch.object(tools.urllib.request, 'urlopen', return_value=io.BytesIO(payload)):
                tools.fetch(Path(temp))
            path = Path(temp) / 'linuxdeploy-x86_64.AppImage'
            self.assertEqual(path.read_bytes(), payload)
            self.assertEqual(path.stat().st_mode & 0o777, 0o755)

    def test_transient_download_failure_retries_and_still_checks_the_digest(self):
        with tempfile.TemporaryDirectory() as temp:
            payload = b'verified retry'
            with mock.patch.object(tools, 'TOOLS', {'linuxdeploy': hashlib.sha256(payload).hexdigest()}), \
                 mock.patch.object(tools.urllib.request, 'urlopen', side_effect=[OSError('temporary network failure'), io.BytesIO(payload)]) as fetch, \
                 mock.patch.object(tools.time, 'sleep'):
                tools.fetch(Path(temp))
                self.assertEqual(fetch.call_count, 2)
            self.assertEqual((Path(temp) / 'linuxdeploy-x86_64.AppImage').read_bytes(), payload)
