# SPDX-License-Identifier: GPL-2.0-or-later
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest import mock

SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))
spec = importlib.util.spec_from_file_location('release_recovery', SCRIPTS / 'recovery.py')
recovery = importlib.util.module_from_spec(spec)
spec.loader.exec_module(recovery)

VERSION = '4.0.0-beta.4'
REPO = 'owner/vlc'
OLD = 'a' * 40
NEW = 'b' * 40


class RecoveryTests(unittest.TestCase):
    def previous_run(self, **updates):
        return dict(head_repository=dict(full_name=REPO), path='.github/workflows/beta-release.yml',
                    event='workflow_dispatch', status='completed', head_sha=OLD, **updates)

    def jobs(self):
        return [dict(name=spec['job'], conclusion='success') for spec in recovery.TARGETS.values()]

    def artifacts(self):
        return [dict(name=f'{VERSION}-{name}', id=index, expired=False)
                for index, name in enumerate(recovery.TARGETS, start=1)]

    def plan(self, run=None, jobs=None, artifacts=None, target='macos-x86_64'):
        def listing(repository, path, field):
            self.assertEqual(repository, REPO)
            return (self.jobs() if jobs is None else jobs) if field == 'jobs' else (
                self.artifacts() if artifacts is None else artifacts)
        with mock.patch.object(recovery, 'api', return_value=run or self.previous_run()), \
                mock.patch.object(recovery, 'pages', side_effect=listing), \
                mock.patch.object(recovery, 'recovery_changes', return_value=['doc/RELEASING.md']):
            return recovery.build_plan(REPO, '1234', target, VERSION, NEW)

    def test_default_build_still_builds_every_target_without_api(self):
        with mock.patch.object(recovery, 'api') as api:
            plan = recovery.build_plan(REPO, '', 'all', VERSION, NEW)
        api.assert_not_called()
        self.assertFalse(plan['recovery'])
        self.assertTrue(plan['build_linux'])
        self.assertTrue(plan['build_windows'])
        self.assertEqual(len(plan['macos_matrix']['include']), 2)
        self.assertEqual(plan['reuse_matrix']['include'], [])

    def test_selective_builds_only_run_selected_target(self):
        for target in recovery.TARGETS:
            with self.subTest(target=target):
                plan = self.plan(target=target)
                reused = {entry['target'] for entry in plan['reuse_matrix']['include']}
                self.assertEqual(reused, set(recovery.TARGETS) - {target})
                self.assertEqual(plan['build_windows'], target == 'win64')
                self.assertEqual(plan['build_linux'], target == 'linux-x86_64')
                self.assertEqual(plan['build_macos'], target.startswith('macos-'))
                if plan['build_macos']:
                    self.assertEqual(plan['macos_matrix']['include'][0]['arch'], target[6:])

    def test_inputs_must_select_a_target_and_run_together(self):
        for previous, target in (('', 'win64'), ('1234', 'all'), ('x;echo bad', 'win64')):
            with self.subTest(previous=previous, target=target), self.assertRaises(ValueError):
                recovery.build_plan(REPO, previous, target, VERSION, NEW)

    def test_failed_selection_rebuilds_intel_and_failed_windows_smoke_test(self):
        jobs = self.jobs()
        jobs[1]['conclusion'] = 'failure'
        jobs[-1]['conclusion'] = 'failure'
        plan = self.plan(jobs=jobs, target='failed')
        self.assertEqual(plan['rebuild_targets'], 'macos-x86_64,win64')
        self.assertEqual(plan['macos_matrix']['include'], [dict(runner='macos-15-intel', arch='x86_64')])
        self.assertTrue(plan['build_windows'])
        self.assertFalse(plan['build_linux'])
        self.assertEqual({p['target'] for p in plan['reuse_matrix']['include']},
                         {'macos-arm64', 'linux-x86_64'})
        with self.assertRaisesRegex(ValueError, 'windows-smoke.*failure.*select failed'):
            self.plan(jobs=jobs)

    def test_failed_selection_handles_missing_artifacts_and_empty_reuse_matrix(self):
        plan = self.plan(artifacts=[], target='failed')
        self.assertFalse(plan['reuse'])
        self.assertEqual(plan['reuse_matrix']['include'], [])
        self.assertTrue(plan['build_linux'])
        self.assertTrue(plan['build_windows'])
        self.assertEqual(len(plan['macos_matrix']['include']), 2)
        with self.assertRaisesRegex(ValueError, 'All targets passed'):
            self.plan(target='failed')

    def test_previous_checks_and_artifacts_must_be_complete(self):
        jobs = self.jobs()
        jobs[-1]['conclusion'] = 'failure'  # ZIP upload alone does not prove Windows startup.
        cases = [dict(jobs=jobs), dict(artifacts=self.artifacts()[1:])]
        expired = self.artifacts()
        expired[0]['expired'] = True
        cases.append(dict(artifacts=expired))
        for updates in (dict(status='in_progress'), dict(event='pull_request'),
                        dict(head_repository=dict(full_name='fork/vlc')),
                        dict(path='.github/workflows/unrelated.yml')):
            run = self.previous_run()
            run.update(updates)
            cases.append(dict(run=run))
        for case in cases:
            with self.subTest(case=case), self.assertRaises(ValueError):
                self.plan(**case)

    def fixture_package(self, directory, target, source):
        spec = recovery.TARGETS[target]
        directory.mkdir(parents=True)
        filename = f'vlc-juxtopposed-{VERSION}-{target}.{spec["extension"]}'
        data = ('archive ' + target).encode()
        digest = hashlib.sha256(data).hexdigest()
        (directory / filename).write_bytes(data)
        (directory / (filename + '.sha256')).write_text(f'{digest}  {filename}\n')
        metadata = dict(tag=VERSION, version=VERSION, source_commit=source,
                        platform=spec['platform'], architecture=spec['architecture'],
                        asset_name=filename, size=len(data), sha256=digest)
        (directory / spec['metadata']).write_text(json.dumps(metadata))
        return metadata

    def test_package_rejects_wrong_provenance_version_target_or_checksum(self):
        for field, invalid in (('source_commit', NEW), ('version', '4.0.0-beta.3'),
                               ('tag', 'beta-4'), ('architecture', 'other'),
                               ('platform', 'windows'), ('asset_name', '../wrong.zip'),
                               ('size', 0), ('sha256', '0' * 64)):
            with self.subTest(field=field), tempfile.TemporaryDirectory() as temp:
                directory = Path(temp) / 'artifact'
                metadata = self.fixture_package(directory, 'macos-arm64', OLD)
                metadata[field] = invalid
                (directory / 'macos-arm64-build.json').write_text(json.dumps(metadata))
                with self.assertRaises(ValueError):
                    recovery.validate_package(directory, 'macos-arm64', VERSION, OLD)
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp) / 'artifact'
            metadata = self.fixture_package(directory, 'win64', OLD)
            (directory / metadata['asset_name']).write_bytes(b'tampered archive')
            with self.assertRaises(ValueError):
                recovery.validate_package(directory, 'win64', VERSION, OLD)

    def make_repository(self, root):
        def command(*args):
            subprocess.run(['git', '-C', str(root), *args], check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        command('init')
        command('config', 'user.email', 'test@example.invalid')
        command('config', 'user.name', 'Release tests')
        (root / f'doc/releases/{VERSION}.md').parent.mkdir(parents=True)
        (root / f'doc/releases/{VERSION}.md').write_text('Release notes\n')
        (root / 'Makefile.am').write_text('EXTRA_DIST = \\\n\textras/package/release/version.py \\\n')
        command('add', '.')
        command('-c', 'commit.gpgsign=false', 'commit', '-m', 'original')
        old = recovery.git('rev-parse', 'HEAD', root=root)
        (root / 'doc/RELEASING.md').write_text('Recovery instructions\n')
        command('add', '.')
        command('-c', 'commit.gpgsign=false', 'commit', '-m', 'packaging fix')
        new = recovery.git('rev-parse', 'HEAD', root=root)
        return old, new, command

    def test_application_changes_and_unrelated_history_require_full_rebuild(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            old, new, command = self.make_repository(root)
            self.assertEqual(recovery.recovery_changes(old, new, root), ['doc/RELEASING.md'])
            (root / 'meson.build').write_text('changed application version')
            command('add', '.')
            command('-c', 'commit.gpgsign=false', 'commit', '-m', 'runtime change')
            with self.assertRaisesRegex(ValueError, 'full build'):
                recovery.recovery_changes(old, 'HEAD', root)
            with self.assertRaises(subprocess.CalledProcessError):
                recovery.recovery_changes(new, old, root)

    def test_makefile_may_only_change_release_script_distribution_entries(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            old, _, command = self.make_repository(root)
            makefile = root / 'Makefile.am'
            makefile.write_text(makefile.read_text() + '\textras/package/release/recovery.py \\\n')
            command('add', '.')
            command('-c', 'commit.gpgsign=false', 'commit', '-m', 'distribute release tooling')
            self.assertIn('Makefile.am', recovery.recovery_changes(old, 'HEAD', root))
            makefile.write_text(makefile.read_text() + 'SUBDIRS = different\n')
            command('add', '.')
            command('-c', 'commit.gpgsign=false', 'commit', '-m', 'change build inputs')
            with self.assertRaisesRegex(ValueError, 'distribution list'):
                recovery.recovery_changes(old, 'HEAD', root)

    def test_windows_release_wrapper_changes_are_treated_as_release_tooling(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            old, _, command = self.make_repository(root)
            wrapper = root / 'extras/package/win32/build.sh'
            wrapper.parent.mkdir(parents=True)
            wrapper.write_text('# release dependency cache hooks\n')
            command('add', '.')
            command('-c', 'commit.gpgsign=false', 'commit', '-m', 'release tooling')
            self.assertIn('extras/package/win32/build.sh', recovery.recovery_changes(old, 'HEAD', root))

    def test_windows_loader_fix_requires_windows_rebuild_but_can_reuse_other_platforms(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            old, _, command = self.make_repository(root)
            loader = root / 'src/win32/plugin.c'
            loader.parent.mkdir(parents=True)
            loader.write_text('Windows-only loader change\n')
            command('add', '.')
            command('-c', 'commit.gpgsign=false', 'commit', '-m', 'Windows loader fix')
            with self.assertRaisesRegex(ValueError, 'rebuild win64'):
                recovery.recovery_changes(old, 'HEAD', root, rebuild_targets={'macos-x86_64'})
            self.assertIn('src/win32/plugin.c',
                          recovery.recovery_changes(old, 'HEAD', root, rebuild_targets={'win64', 'macos-x86_64'}))
            (root / 'meson.build').write_text('shared application change')
            command('add', '.')
            command('-c', 'commit.gpgsign=false', 'commit', '-m', 'shared runtime fix')
            with self.assertRaisesRegex(ValueError, 'full build'):
                recovery.recovery_changes(old, 'HEAD', root, rebuild_targets={'win64', 'macos-x86_64'})

    def test_assembly_preserves_sources_and_produces_complete_manifest_and_checksums(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            old, new, _ = self.make_repository(root)
            artifacts = root / 'artifacts'
            output = root / 'complete'
            for target in recovery.TARGETS:
                self.fixture_package(artifacts / f'{VERSION}-{target}', target,
                                     new if target == 'macos-x86_64' else old)
            manifest = recovery.assemble(artifacts, output, VERSION, new, '5678', REPO,
                                         '1234', 'macos-x86_64', old, root)
            self.assertEqual(len(manifest['packages']), 4)
            for package in manifest['packages']:
                rebuilt = package['target'] == 'macos-x86_64'
                self.assertEqual(package['reused'], not rebuilt)
                self.assertEqual(package['source_commit'], new if rebuilt else old)
                self.assertEqual(package['run_id'], '5678' if rebuilt else '1234')
            self.assertEqual((output / 'SOURCE_COMMIT.txt').read_text(), new + '\n')
            self.assertEqual(json.loads((output / 'RELEASE_MANIFEST.json').read_text()), manifest)
            with tarfile.open(output / f'vlc-juxtopposed-{VERSION}-source-{old}.tar.gz') as archive:
                self.assertFalse(any(name.endswith('/doc/RELEASING.md') for name in archive.getnames()))
            with tarfile.open(output / f'vlc-juxtopposed-{VERSION}-source.tar.gz') as archive:
                self.assertTrue(any(name.endswith('/doc/RELEASING.md') for name in archive.getnames()))
            notes = (output / 'RELEASE_NOTES.md').read_text()
            self.assertIn(f'`{old}`', notes)
            self.assertIn(f'`{new}`', notes)
            for line in (output / 'SHA256SUMS.txt').read_text().splitlines():
                digest, filename = line.split('  ')
                self.assertEqual(digest, recovery.file_digest(output / filename))
            # An incomplete set must not produce a "complete" output directory.
            (artifacts / f'{VERSION}-win64/windows-build.json').unlink()
            with self.assertRaises(OSError):
                recovery.assemble(artifacts, root / 'incomplete', VERSION, new, '5678', REPO,
                                  '1234', 'macos-x86_64', old, root)
            self.assertFalse((root / 'incomplete').exists())

    def test_existing_release_tags_cannot_be_moved(self):
        with mock.patch.object(recovery, 'api', return_value=None):
            recovery.check_release_tag(REPO, VERSION, NEW)
        for kind in ('commit', 'tag'):
            refs = [dict(object=dict(type=kind, sha=NEW))]
            if kind == 'tag':
                refs.append(dict(object=dict(type='commit', sha=NEW)))
            with mock.patch.object(recovery, 'api', side_effect=refs):
                recovery.check_release_tag(REPO, VERSION, NEW)
        with mock.patch.object(recovery, 'api', return_value=dict(object=dict(type='commit', sha=OLD))):
            with self.assertRaisesRegex(ValueError, 'never moved'):
                recovery.check_release_tag(REPO, VERSION, NEW)

    def test_assembly_accepts_two_rebuilt_targets_with_two_reused_packages(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            old, new, _ = self.make_repository(root)
            rebuilt = {'macos-x86_64', 'win64'}
            for name in recovery.TARGETS:
                self.fixture_package(root / 'artifacts' / f'{VERSION}-{name}', name,
                                     new if name in rebuilt else old)
            manifest = recovery.assemble(root / 'artifacts', root / 'complete', VERSION, new,
                                         '5678', REPO, '1234', 'macos-x86_64,win64', old, root)
            self.assertEqual({p['target'] for p in manifest['packages'] if not p['reused']}, rebuilt)
            self.assertEqual({p['source_commit'] for p in manifest['packages'] if p['reused']}, {old})

    def test_plan_cli_outputs_can_drive_github_job_conditions(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / 'github-output'
            with mock.patch.dict(os.environ, {'GITHUB_OUTPUT': str(output), 'REBUILD_TARGET': 'all',
                                             'PREVIOUS_RUN_ID': '', 'CREATE_DRAFT': 'false'}), \
                    mock.patch.object(sys, 'argv', ['recovery.py', 'plan']), \
                    mock.patch.object(recovery, 'git', return_value=NEW), \
                    mock.patch.object(recovery, 'api') as api, \
                    mock.patch('sys.stdout', new_callable=io.StringIO):
                recovery.main()
            api.assert_not_called()
            values = dict(line.split('=', 1) for line in output.read_text().splitlines())
            self.assertEqual(values['build_macos'], 'true')
            self.assertEqual(values['recovery'], 'false')
            self.assertEqual(values['previous_run'], '')
            self.assertEqual(len(json.loads(values['macos_matrix'])['include']), 2)

    def test_manual_draft_is_pinned_to_current_commit_and_never_published(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp)
            (output / 'RELEASE_NOTES.md').write_text('Notes')
            (output / 'RELEASE_MANIFEST.json').write_text('{}')
            (output / 'package.zip').touch()
            with mock.patch.object(sys, 'argv', ['recovery.py', 'publish', '--repository', REPO,
                                                '--output', str(output)]), \
                    mock.patch.object(recovery, 'git', return_value=NEW), \
                    mock.patch.object(recovery, 'check_release_tag') as check, \
                    mock.patch.object(recovery.subprocess, 'run') as tool:
                recovery.main()
            check.assert_called_once_with(REPO, VERSION, NEW)
            args = tool.call_args.args[0]
            self.assertEqual(args[:8], ['gh', 'release', 'create', VERSION, '--target', NEW,
                                       '--draft', '--prerelease'])
            self.assertIn(str(output / 'RELEASE_MANIFEST.json'), args)
            self.assertTrue(tool.call_args.kwargs['check'])


if __name__ == '__main__':
    unittest.main()
