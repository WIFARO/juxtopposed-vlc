#!/usr/bin/env python3
"""Fingerprint and validate the installed Windows dependency SDK, never VLC output."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
WORK = Path('contrib/contrib-release-win64')
PREFIX = WORK / 'contrib/x86_64-w64-mingw32'
MARKER = Path('build-release-cache/windows-dependencies.json')
PATHS = [WORK / 'extras/tools/build', PREFIX,
         *[WORK / 'contrib' / name for name in ('bin', 'include', 'lib', 'lib64', 'libexec', 'share')],
         WORK / 'contrib/contrib-win64/vulkan-loader/LICENSE.txt',
         WORK / 'contrib/contrib-win64/vulkan-loader/LICENSES']
REQUIRED = [PREFIX / 'lib/pkgconfig/Qt6Core.pc', PREFIX / 'lib/pkgconfig/Qt6Qml.pc',
            PREFIX / 'lib/pkgconfig/medialibrary.pc', PREFIX / 'bin/qmake6',
            PREFIX / 'share/meson/cross/contrib.ini',
            WORK / 'contrib/contrib-win64/vulkan-loader/LICENSE.txt']
INPUTS = ['contrib', 'extras/tools', 'extras/package/win32',
          'extras/package/release/build-windows.sh',
          'extras/package/release/dependency-cache.py', '.github/workflows/beta-release.yml']


def digest_file(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def cache_key(toolchain, root=ROOT):
    if not toolchain or '@sha256:' not in toolchain:
        raise ValueError('A digest-pinned Windows toolchain image is required')
    digest = hashlib.sha256(('windows-x86_64-sdk-v1\0/src\0' + toolchain).encode())
    names = subprocess.check_output(['git', '-C', str(root), 'ls-files', '-z', '--', *INPUTS])
    for name in sorted(names.decode().strip('\0').split('\0')):
        if name:
            digest.update(name.encode() + b'\0' + (root / name).read_bytes() + b'\0')
    return 'release-win64-deps-v1-' + digest.hexdigest()


def download_key(root=ROOT):
    digest = hashlib.sha256(b'windows-source-archives-v1\0')
    names = subprocess.check_output(['git', '-C', str(root), 'ls-files', '-z', '--', 'contrib'])
    for name in sorted(names.decode().strip('\0').split('\0')):
        if name.endswith('/SHA512SUMS'):
            digest.update(name.encode() + b'\0' + (root / name).read_bytes() + b'\0')
    return 'release-win64-downloads-v1-' + digest.hexdigest()


def inventory(root=ROOT):
    for relative in REQUIRED:
        if not (root / relative).is_file():
            raise ValueError(f'Incomplete Windows dependency SDK: {relative}')
    files = {}
    for relative in PATHS:
        path = root / relative
        if path.is_symlink():
            raise ValueError(f'Dependency cache root must not be a symbolic link: {relative}')
        entries = [path, *path.rglob('*')] if path.is_dir() else [path]
        for entry in sorted(entries):
            name = entry.relative_to(root).as_posix()
            if entry.is_symlink():
                files[name] = dict(link=os.readlink(entry))
            elif entry.is_file():
                files[name] = dict(size=entry.stat().st_size, sha256=digest_file(entry),
                                   mode=stat.S_IMODE(entry.stat().st_mode))
    return files


def seal(key, root=ROOT):
    marker = root / MARKER
    data = dict(key=key, files=inventory(root))
    marker.parent.mkdir(parents=True, exist_ok=True)
    marker.write_text(json.dumps(data, sort_keys=True) + '\n')


def check(key, root=ROOT):
    try:
        marker = root / MARKER
        if marker.is_symlink():
            return False
        data = json.loads(marker.read_text())
        return isinstance(data, dict) and data.get('key') == key and data.get('files') == inventory(root)
    except (OSError, ValueError):
        return False


def clean(root=ROOT):
    # Discard installed dependencies and their old build stamps together.
    # Application sources/builds and shared downloads are outside this list.
    for relative in [*PATHS, MARKER, WORK / 'extras/tools', WORK / 'contrib/contrib-win64']:
        path = root / relative
        if path.is_symlink() or path.is_file():
            path.unlink()
        elif path.is_dir():
            shutil.rmtree(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['key', 'check', 'seal', 'clean'])
    parser.add_argument('--toolchain', default=os.environ.get('WINDOWS_BUILD_IMAGE', ''))
    parser.add_argument('--key', default=os.environ.get('RELEASE_DEPS_CACHE_KEY', ''))
    args = parser.parse_args()
    try:
        if args.command == 'key':
            key = cache_key(args.toolchain)
            print(key)
            if os.environ.get('GITHUB_OUTPUT'):
                with open(os.environ['GITHUB_OUTPUT'], 'a') as stream:
                    stream.write(f'key={key}\ndownload_key={download_key()}\npaths<<VLC_DEPENDENCY_PATHS\n')
                    stream.write('\n'.join(str(p) for p in [*PATHS, MARKER]))
                    stream.write('\nVLC_DEPENDENCY_PATHS\n')
        elif args.command == 'clean':
            clean()
        elif not args.key:
            parser.error('A dependency cache key is required')
        elif args.command == 'seal':
            seal(args.key)
        elif not check(args.key):
            print('No complete Windows dependency SDK matches this build')
            return 1
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.error(str(error))
    return 0


if __name__ == '__main__':
    sys.exit(main())
