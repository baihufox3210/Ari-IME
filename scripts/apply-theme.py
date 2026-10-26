#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-KYL-Noncommercial-1.0
"""Select Ari's installed Fcitx Classic UI theme without replacing other settings."""
import argparse
from datetime import datetime, timezone
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def update_root_options(text, options):
    """Change only top-level Fcitx INI keys; preserve comments and subsections."""
    pending = dict(options)
    lines = text.splitlines(keepends=True)
    result = []
    in_section = False
    for line in lines:
        stripped = line.strip()
        if not in_section and stripped.startswith('['):
            if result and not result[-1].endswith('\n'):
                result[-1] += '\n'
            result.extend(f'{key}={value}\n' for key, value in pending.items())
            pending.clear()
            in_section = True
        if not in_section and not stripped.startswith(('#', ';')) and '=' in line:
            key = line.split('=', 1)[0].strip()
            if key in options:
                if key in pending:
                    result.append(f'{key}={pending.pop(key)}\n')
                continue  # collapse duplicate keys instead of leaving ambiguity
        result.append(line)
    if result and not result[-1].endswith('\n'):
        result[-1] += '\n'
    result.extend(f'{key}={value}\n' for key, value in pending.items())
    return ''.join(result)


def theme_exists(name):
    user_data = Path(os.environ.get('XDG_DATA_HOME') or Path.home() / '.local/share')
    system_data = os.environ.get('XDG_DATA_DIRS') or '/usr/local/share:/usr/share'
    roots = [user_data] + [Path(p) for p in system_data.split(':') if p]
    return any((root / 'fcitx5/themes' / name / 'theme.conf').is_file() for root in roots)


def apply_theme(mode, path, font=None, dry_run=False):
    options = {
        'Theme': 'ari-dark' if mode == 'dark' else 'ari-light',
        'DarkTheme': 'ari-dark',
        'UseDarkTheme': 'True' if mode == 'auto' else 'False',
    }
    if font is not None:
        if not font.strip() or '\n' in font or '\r' in font:
            raise ValueError('Font must be a non-empty single line')
        options['Font'] = font
    # Respect dotfile symlinks: update their target rather than replacing links.
    path = path.expanduser().resolve()
    original = path.read_text(encoding='utf-8') if path.exists() else ''
    updated = update_root_options(original, options)
    if dry_run:
        print(updated, end='')
        return None
    if updated == original:
        return None
    path.parent.mkdir(parents=True, exist_ok=True)
    backup = None
    if path.exists():
        stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
        backup = path.with_name(path.name + '.ari-backup-' + stamp)
        shutil.copy2(path, backup)
    temp_name = None
    try:
        with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8',
                                         dir=path.parent, delete=False) as temp:
            temp_name = temp.name
            temp.write(updated)
            temp.flush()
            os.fsync(temp.fileno())
        if path.exists():
            shutil.copymode(path, temp_name)
        os.replace(temp_name, path)
    finally:
        if temp_name and os.path.exists(temp_name):
            os.unlink(temp_name)
    return backup


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('dark', 'light', 'auto'))
    parser.add_argument('--font', help='Optional Fcitx font, e.g. "Sans 12"')
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('--no-reload', action='store_true')
    args = parser.parse_args()
    if not args.dry_run:
        required = ('ari-light', 'ari-dark') if args.mode == 'auto' else ('ari-' + args.mode,)
        missing = [name for name in required if not theme_exists(name)]
        if missing:
            parser.error('Install Ari first; missing theme(s): ' + ', '.join(missing))
    config = Path(os.environ.get('XDG_CONFIG_HOME') or Path.home() / '.config')
    path = config / 'fcitx5/conf/classicui.conf'
    try:
        backup = apply_theme(args.mode, path, args.font, args.dry_run)
    except (OSError, ValueError) as error:
        parser.exit(1, f'ari-ime-theme: {error}\n')
    if args.dry_run:
        return
    print('Classic UI theme:', args.mode)
    print('This applies to all input methods using Fcitx Classic UI.')
    if backup:
        print('Previous configuration:', backup)
    if not args.no_reload:
        remote = shutil.which('fcitx5-remote')
        if not remote:
            print('Reload later with: fcitx5-remote -r', file=sys.stderr)
        elif subprocess.run([remote, '-r'], check=False).returncode:
            print('Reload failed; restart Fcitx5 to apply the theme.', file=sys.stderr)


if __name__ == '__main__':
    main()
