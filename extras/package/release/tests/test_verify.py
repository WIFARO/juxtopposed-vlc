# SPDX-License-Identifier: GPL-2.0-or-later
import contextlib
import io
from pathlib import Path
import runpy
import subprocess
import sys
import unittest
from unittest import mock

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))


class VerifyTests(unittest.TestCase):
    def test_reported_banner_reaches_playback_checks(self):
        def command(args, **kwargs):
            if '--version' in args:
                output = 'VLC version 4.0.0-beta.4 Otto Chriek (4.0.0-dev-40063-ge93a0c3478)\n'
            elif '--list' in args:
                output = 'qt Qt interface\navcodec FFmpeg decoder\nmedialibrary Media library\n'
            else:
                output = ('using audio decoder module "avcodec"\n'
                          'using interface module "qt"\n'
                          'using video decoder module "avcodec"\n'
                          'using vout window module "qt"\n'
                          'using vout display module "opengl"\n')
            return subprocess.CompletedProcess(args, 0, stdout=output)

        argv = ['verify.py', '/path with spaces/vlc', '--expected-version', '4.0.0-beta.4',
                '--sample', '/sample.mkv', '--gui', '--video']
        with mock.patch.object(sys, 'argv', argv), \
             mock.patch.object(subprocess, 'run', side_effect=command) as run, \
             contextlib.redirect_stdout(io.StringIO()) as output:
            runpy.run_path(str(SCRIPTS / 'verify.py'), run_name='__main__')
        self.assertEqual(run.call_count, 5)
        gui_calls = [call for call in run.call_args_list if '-I' in call.args[0] and 'qt' in call.args[0]]
        self.assertEqual(len(gui_calls), 2)
        self.assertEqual(gui_calls[0].kwargs['env'], gui_calls[1].kwargs['env'])
        self.assertIn('playback smoke checks passed', output.getvalue())

    def run_gui(self, outputs, require_opengl=False):
        calls = []
        def command(args, **kwargs):
            calls.append(args)
            if '--version' in args:
                text = 'VLC version 4.0.0-beta.4 Otto Chriek\n'
            elif '--list' in args:
                text = 'qt Qt\navcodec FFmpeg\nmedialibrary Library\n'
            elif 'qt' not in args:
                text = 'using audio decoder module "avcodec"\n'
            else:
                text = ('using interface module "qt"\nusing video decoder module "avcodec"\n'
                        'using vout window module "qt"\nusing vout display module "gl"\n') + outputs.pop(0)
            return subprocess.CompletedProcess(args, 0, stdout=text)
        argv = ['verify.py', '/vlc', '--sample', '/sample.mkv', '--gui', '--video']
        if require_opengl:
            argv.append('--require-opengl')
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(subprocess, 'run', side_effect=command), \
             contextlib.redirect_stdout(io.StringIO()):
            runpy.run_path(str(SCRIPTS / 'verify.py'), run_name='__main__')
        return calls

    def test_second_start_rejects_reported_graphics_failures_even_with_exit_zero(self):
        for message in ('Failed to load client buffer integration: "wayland-egl"',
                        'Compositor: Scene Graph Error: QQuickWindow::ContextNotAvailable',
                        'Failed to initialize graphics backend for OpenGL.'):
            with self.subTest(message=message), self.assertRaisesRegex(SystemExit, 'Qt interface reported'):
                self.run_gui(['', message])

    def test_requesting_opengl_must_prove_a_working_context(self):
        with self.assertRaisesRegex(SystemExit, 'did not confirm a working OpenGL'):
            self.run_gui(['Creating QRhi with backend OpenGL'], True)
        self.run_gui(['Created OpenGL context QSurfaceFormat\nCreated QRhi 0x123\n'] * 2, True)
        self.run_gui(['Using existing QOpenGLContext 0x123\nCreated QRhi 0x456\n'] * 2, True)

    def test_wrong_version_stops_before_module_and_playback_checks(self):
        result = subprocess.CompletedProcess([], 0, stdout='VLC version 4.0.0-beta.3 Otto Chriek\n')
        argv = ['verify.py', '/vlc', '--expected-version', '4.0.0-beta.4']
        with mock.patch.object(sys, 'argv', argv), \
             mock.patch.object(subprocess, 'run', return_value=result) as run, \
             self.assertRaisesRegex(SystemExit, 'expected 4.0.0-beta.4; got 4.0.0-beta.3'):
            runpy.run_path(str(SCRIPTS / 'verify.py'), run_name='__main__')
        self.assertEqual(run.call_count, 1)
