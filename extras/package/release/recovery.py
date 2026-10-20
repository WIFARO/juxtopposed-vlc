#!/usr/bin/env python3
"""Plan selective release builds and assemble packages with their original provenance."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import urllib.error
import urllib.request

from version import ROOT, validate_tag

TARGETS = {
    'macos-arm64': dict(platform='macos', architecture='arm64', extension='zip',
                        metadata='macos-arm64-build.json', job='macos (macos-15, arm64)'),
    'macos-x86_64': dict(platform='macos', architecture='x86_64', extension='zip',
                         metadata='macos-x86_64-build.json', job='macos (macos-15-intel, x86_64)'),
    'linux-x86_64': dict(platform='linux', architecture='x86_64', extension='tar.gz',
                         metadata='linux-build.json', job='linux'),
    'win64': dict(platform='windows', architecture='x86_64', extension='zip',
                  metadata='windows-build.json', job='windows-smoke'),
}


def git(*args, root=ROOT):
    return subprocess.check_output(['git', '-C', str(root), *args], text=True).strip()


def api(repository, path, optional=False):
    if not re.fullmatch(r'[\w.-]+/[\w.-]+', repository):
        raise ValueError('Expected an owner/repository name')
    request = urllib.request.Request(f'https://api.github.com/repos/{repository}/{path}', headers={
        'Accept': 'application/vnd.github+json', 'User-Agent': 'vlc-release-recovery',
        **({'Authorization': f'Bearer {os.environ["GH_TOKEN"]}'} if os.environ.get('GH_TOKEN') else {}),
    })
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            return json.load(response)
    except urllib.error.HTTPError as error:
        if optional and error.code == 404:
            return None
        raise


def pages(repository, path, field):
    result = []
    for page in range(1, 100):
        batch = api(repository, f'{path}?per_page=100&page={page}')[field]
        result.extend(batch)
        if len(batch) < 100:
            return result
    raise ValueError(f'Too many pages in {path}')


def recovery_changes(source, current, root=ROOT, rebuild_targets=()):
    subprocess.run(['git', '-C', str(root), 'merge-base', '--is-ancestor', source, current], check=True)
    paths = git('diff', '--no-ext-diff', '--no-renames', '--name-only', source, current, root=root).splitlines()
    # Reusing packages is only supported across release tooling/documentation
    # changes. Application, version and dependency changes require full builds.
    forbidden = [p for p in paths if not (
        p.startswith(('extras/package/release/', 'doc/')) or
        p in ('.github/workflows/beta-release.yml', 'extras/package/win32/build.sh', 'Makefile.am') or
        ('win64' in rebuild_targets and p in ('bin/winvlc.c', 'src/win32/plugin.c')))]
    if 'Makefile.am' in paths:
        diff = git('diff', '--no-ext-diff', '--unified=0', source, current, '--', 'Makefile.am', root=root)
        edits = [line[1:] for line in diff.splitlines()
                 if line.startswith(('+', '-')) and not line.startswith(('+++', '---'))]
        if any(not re.fullmatch(r'\s*extras/package/release/[\w./-]+\s*\\?', line) for line in edits):
            forbidden.append('Makefile.am (changes outside the release-script distribution list)')
    if forbidden:
        if any(p in ('bin/winvlc.c', 'src/win32/plugin.c') for p in forbidden):
            raise ValueError('Windows loader changed: rebuild win64 too (select failed to rebuild all failed targets)')
        raise ValueError('Application/build inputs changed; run a full build: ' + ', '.join(forbidden))
    return paths


def selected_targets(value):
    targets = set(TARGETS) if value == 'all' else set(value.split(','))
    if not targets or not targets.issubset(TARGETS):
        raise ValueError(f'Unknown rebuild target selection: {value}')
    return targets


def build_plan(repository, previous_run, target, version, current, root=ROOT):
    if bool(previous_run) != (target != 'all'):
        raise ValueError('Select rebuild targets and provide a previous run ID together, or use all with no run ID')
    selected = selected_targets(target) if target != 'failed' else set()
    reused = []
    source = current
    changes = []
    if previous_run:
        if not re.fullmatch(r'[1-9][0-9]*', previous_run):
            raise ValueError('The previous run ID must be a positive integer')
        run = api(repository, f'actions/runs/{previous_run}')
        if (run['head_repository']['full_name'] != repository or
                run['path'] != '.github/workflows/beta-release.yml' or
                run['event'] not in ('push', 'workflow_dispatch')):
            raise ValueError('Reuse requires a Build release run from this repository')
        if run['status'] != 'completed':
            raise ValueError('Wait for the previous run to finish, including Windows startup checks')
        source = run['head_sha']
        jobs = pages(repository, f'actions/runs/{previous_run}/jobs', 'jobs')
        artifacts = pages(repository, f'actions/runs/{previous_run}/artifacts', 'artifacts')
        def verified(name):
            return any(j['name'] == TARGETS[name]['job'] and j['conclusion'] == 'success' for j in jobs)
        def matching_artifacts(name):
            return [a for a in artifacts if a['name'] == f'{version}-{name}' and not a['expired']]
        if target == 'failed':
            selected = {name for name in TARGETS if not verified(name) or len(matching_artifacts(name)) != 1}
            if not selected:
                raise ValueError('All targets passed and have artifacts; select a specific target to rebuild')
        changes = recovery_changes(source, current, root, rebuild_targets=selected)
        for name, spec in TARGETS.items():
            if name in selected:
                continue
            if not verified(name):
                states = ', '.join(j['conclusion'] or j['status'] for j in jobs if j['name'] == spec['job']) or 'missing'
                raise ValueError(f'Cannot reuse {name}: {spec["job"]} in run {previous_run} is {states}; '
                                 'select failed to rebuild all failed targets')
            matches = matching_artifacts(name)
            if len(matches) != 1:
                raise ValueError(f'Missing, expired or ambiguous artifact for {version}-{name}')
            reused.append(dict(target=name, artifact_id=str(matches[0]['id'])))
    macs = [dict(runner='macos-15' if name == 'macos-arm64' else 'macos-15-intel',
                 arch=TARGETS[name]['architecture']) for name in TARGETS
            if name.startswith('macos-') and name in selected]
    return dict(recovery=bool(previous_run), previous_run=previous_run, source_commit=source,
                reuse=bool(reused), rebuild_targets=','.join(name for name in TARGETS if name in selected),
                macos_matrix=dict(include=macs), build_macos=bool(macs),
                build_linux='linux-x86_64' in selected,
                build_windows='win64' in selected,
                reuse_matrix=dict(include=reused), changed_paths=changes)


def validate_package(directory, target, version, source):
    spec = TARGETS[target]
    if (directory / spec['metadata']).is_symlink():
        raise ValueError(f'{target}: symbolic links are not valid package metadata')
    metadata = json.loads((directory / spec['metadata']).read_text())
    asset_name = f'vlc-juxtopposed-{version}-{target}.{spec["extension"]}'
    for key, expected in dict(tag=version, version=version, platform=spec['platform'],
                              architecture=spec['architecture'], source_commit=source,
                              asset_name=asset_name).items():
        if metadata.get(key) != expected:
            raise ValueError(f'{target}: {key} must be {expected!r}, got {metadata.get(key)!r}')
    archive = directory / asset_name
    if archive.is_symlink() or not archive.is_file() or archive.stat().st_size == 0:
        raise ValueError(f'{target}: missing or empty archive')
    if metadata.get('size') != archive.stat().st_size:
        raise ValueError(f'{target}: archive size mismatch')
    digest = file_digest(archive)
    if metadata.get('sha256') != digest:
        raise ValueError(f'{target}: archive checksum mismatch')
    checksum = archive.with_name(asset_name + '.sha256')
    if checksum.is_symlink() or checksum.read_text() != f'{digest}  {asset_name}\n':
        raise ValueError(f'{target}: checksum file mismatch')
    return metadata


def file_digest(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def assemble(artifacts, output, version, current, run_id, repository, previous_run='',
             target='all', source='', root=ROOT):
    if output.exists():
        raise ValueError(f'Output already exists: {output}')
    packages = []
    selected = selected_targets(target)
    # Validate the whole set before producing anything that looks complete.
    for name in TARGETS:
        reused = bool(previous_run) and name not in selected
        commit = source if reused else current
        if not re.fullmatch(r'[0-9a-f]{40}', commit):
            raise ValueError(f'{name}: expected a full source commit SHA')
        directory = artifacts / f'{version}-{name}'
        metadata = validate_package(directory, name, version, commit)
        packages.append(dict(target=name, source_commit=commit, reused=reused,
                             run_id=str(previous_run if reused else run_id),
                             asset_name=metadata['asset_name'], size=metadata['size'],
                             sha256=metadata['sha256']))
    output.mkdir(parents=True)
    for package in packages:
        name = package['target']
        directory = artifacts / f'{version}-{name}'
        filenames = [package['asset_name'], package['asset_name'] + '.sha256', TARGETS[name]['metadata']]
        if name.startswith('macos-'):
            audit = f'{name}-dependency-audit.json'
            if (directory / audit).is_file():
                filenames.append(audit)
        for filename in filenames:
            path = directory / filename
            if path.is_symlink():
                raise ValueError(f'Artifact contains a symbolic link: {path}')
            shutil.copy2(path, output / filename)
    manifest = dict(version=version, packaging_commit=current, run_id=str(run_id),
                    previous_run_id=str(previous_run) if previous_run else None, packages=packages)
    (output / 'RELEASE_MANIFEST.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (output / 'SOURCE_COMMIT.txt').write_text(current + '\n')
    # Supply the exact sources for reused binaries as well as the packaging commit.
    for commit in sorted({current} | {p['source_commit'] for p in packages}):
        suffix = '' if commit == current else f'-{commit}'
        filename = f'vlc-juxtopposed-{version}-source{suffix}.tar.gz'
        with (output / filename).open('wb') as stream:
            subprocess.run(['git', '-C', str(root), 'archive', '--format=tar.gz',
                            f'--prefix=vlc-juxtopposed-{version}-source/', commit], stdout=stream, check=True)
    notes = (root / f'doc/releases/{version}.md').read_text().rstrip()
    notes += f'\n\nRelease packaging commit: `{current}`\n\n'
    notes += '| Package | Binary source commit | Build run |\n| --- | --- | --- |\n'
    for package in packages:
        url = f'https://github.com/{repository}/actions/runs/{package["run_id"]}'
        origin = 'reused' if package['reused'] else 'built'
        notes += f'| {package["target"]} | `{package["source_commit"]}` | [{origin}]({url}) |\n'
    (output / 'RELEASE_NOTES.md').write_text(notes)
    sums = ''.join(f'{file_digest(path)}  {path.name}\n' for path in sorted(output.iterdir()))
    (output / 'SHA256SUMS.txt').write_text(sums)
    return manifest


def check_release_tag(repository, version, current):
    ref = api(repository, f'git/ref/tags/{version}', optional=True)
    if ref is None:
        return  # gh release create --target pins a new tag to this commit.
    obj = ref['object']
    for _ in range(10):
        if obj['type'] != 'tag':
            break
        obj = api(repository, f'git/tags/{obj["sha"]}')['object']
    if obj['type'] != 'commit' or obj['sha'] != current:
        raise ValueError(f'Tag {version} already points elsewhere; existing release tags are never moved')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['plan', 'validate', 'assemble', 'publish'])
    parser.add_argument('--previous-run', default=os.environ.get('PREVIOUS_RUN_ID', ''))
    parser.add_argument('--target', default=os.environ.get('REBUILD_TARGET') or 'all')
    parser.add_argument('--repository', default=os.environ.get('GITHUB_REPOSITORY', ''))
    parser.add_argument('--source', default=os.environ.get('REUSED_SOURCE_COMMIT', ''))
    parser.add_argument('--directory', type=Path, default=Path('release-artifacts'))
    parser.add_argument('--output', type=Path, default=Path('downloads'))
    args = parser.parse_args()
    try:
        version = validate_tag(os.environ.get('RELEASE_TAG') or None)['version']
        current = git('rev-parse', 'HEAD')
        if args.command == 'plan':
            plan = build_plan(args.repository, args.previous_run, args.target, version, current)
            print(json.dumps(plan, indent=2))
            if os.environ.get('CREATE_DRAFT') == 'true':
                check_release_tag(args.repository, version, current)
            with open(os.environ['GITHUB_OUTPUT'], 'a') as stream:
                for key, value in plan.items():
                    stream.write(f'{key}={json.dumps(value, separators=(",", ":")) if not isinstance(value, str) else value}\n')
        elif args.command == 'validate':
            validate_package(args.directory, args.target, version, args.source)
        elif args.command == 'assemble':
            assemble(args.directory, args.output, version, current, os.environ['GITHUB_RUN_ID'],
                     args.repository, args.previous_run, args.target, args.source)
        else:
            check_release_tag(args.repository, version, current)
            flags = ['--prerelease'] if validate_tag(version)['channel'] != 'stable' else []
            assets = [str(p) for p in sorted(args.output.iterdir()) if p.name != 'RELEASE_NOTES.md']
            subprocess.run(['gh', 'release', 'create', version, '--target', current, '--draft', *flags,
                            '--title', f'VLC Juxtopposed Redesign - {version}',
                            '--notes-file', str(args.output / 'RELEASE_NOTES.md'), *assets], check=True)
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
