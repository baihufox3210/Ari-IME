#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-KYL-Noncommercial-1.0
"""Select Ari's installed Fcitx Classic UI theme without replacing other settings."""
import argparse
from datetime import datetime, timezone
import os
from pathlib import Path
import shutil
import shlex
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


def update_section_options(text, section, options):
    """Preserve other Fcitx sections, comments and keybindings."""
    lines = text.splitlines(keepends=True)
    result = []
    found = False
    index = 0
    while index < len(lines):
        line = lines[index]
        result.append(line)
        index += 1
        if line.strip() != '[' + section + ']':
            continue
        found = True
        end = index
        while end < len(lines) and not lines[end].strip().startswith('['):
            end += 1
        if not result[-1].endswith('\n'):
            result[-1] += '\n'
        result.append(update_root_options(''.join(lines[index:end]), options))
        index = end
    if not found:
        if result and not result[-1].endswith('\n'):
            result[-1] += '\n'
        result.append('[' + section + ']\n' + update_root_options('', options))
    return ''.join(result)


def theme_exists(name):
    user_data = Path(os.environ.get('XDG_DATA_HOME') or Path.home() / '.local/share')
    system_data = os.environ.get('XDG_DATA_DIRS') or '/usr/local/share:/usr/share'
    roots = [user_data] + [Path(p) for p in system_data.split(':') if p]
    return any((root / 'fcitx5/themes' / name / 'theme.conf').is_file() for root in roots)


def apply_theme(mode, path, font=None, dry_run=False, tray_icon=None):
    options = {
        'Theme': 'ari-dark' if mode == 'dark' else 'ari-light',
        'DarkTheme': 'ari-dark',
        'UseDarkTheme': 'True' if mode == 'auto' else 'False',
    }
    if tray_icon is not None:
        if tray_icon not in ('image', 'text'):
            raise ValueError('Tray icon must be image or text')
        options['PreferTextIcon'] = 'True' if tray_icon == 'text' else 'False'
    if font is not None:
        if not font.strip() or '\n' in font or '\r' in font:
            raise ValueError('Font must be a non-empty single line')
        options['Font'] = font
    return update_config(path, lambda text: update_root_options(text, options), dry_run)


def apply_mode_hint(path, mode, dry_run=False):
    if mode not in ('compact', 'full'):
        raise ValueError('Mode hint must be compact or full')
    options = {'CompactInputMethodInformation': 'True' if mode == 'compact' else 'False'}
    return update_config(path, lambda text: update_section_options(text, 'Behavior', options), dry_run)


def update_config(path, transform, dry_run=False):
    # Respect dotfile symlinks: update their target rather than replacing links.
    path = path.expanduser().resolve()
    original = path.read_text(encoding='utf-8') if path.exists() else ''
    updated = transform(original)
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


def controller_call(method, *args):
    """Use the addon-specific API; remote -r only reloads global settings."""
    busctl = shutil.which('busctl')
    gdbus = shutil.which('gdbus')
    if busctl:
        command = [busctl, '--user', '--timeout=3', 'call',
                   'org.fcitx.Fcitx5', '/controller',
                   'org.fcitx.Fcitx.Controller1', method]
        if args:
            command += ['s', *args]
    elif gdbus:
        command = [gdbus, 'call', '--session', '--timeout', '3',
                   '--dest', 'org.fcitx.Fcitx5', '--object-path', '/controller',
                   '--method', 'org.fcitx.Fcitx.Controller1.' + method, *args]
    else:
        raise RuntimeError('Neither busctl nor gdbus is available')
    result = subprocess.run(command, capture_output=True, text=True, timeout=5)
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or 'Fcitx D-Bus call failed')
    if method == 'CurrentUI':
        if busctl:
            fields = shlex.split(result.stdout)
            if len(fields) == 2 and fields[0] == 's':
                return fields[1]
        else:
            import ast
            value = ast.literal_eval(result.stdout.strip())
            if isinstance(value, tuple) and len(value) == 1 and isinstance(value[0], str):
                return value[0]
        raise RuntimeError('Unrecognized CurrentUI response')
    return result.stdout.strip()


def reload_theme():
    try:
        controller_call('ReloadAddonConfig', 'classicui')
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f'Theme saved, but Classic UI reload failed: {error}', file=sys.stderr)
        print('Restart Fcitx5 after finishing composition to apply it.', file=sys.stderr)
        return False
    print('Classic UI configuration reloaded.')
    try:
        current = controller_call('CurrentUI')
    except (OSError, RuntimeError, ValueError, SyntaxError, subprocess.TimeoutExpired) as error:
        print(f'Cannot verify active candidate UI: {error}', file=sys.stderr)
        return False
    print('Active candidate UI:', current or '(none)')
    if current != 'classicui':
        print('Ari themes require Classic UI. The active desktop panel may override '
              'these colors and corners. Select Classic UI in Fcitx addon settings.',
              file=sys.stderr)
        return False
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('dark', 'light', 'auto'))
    parser.add_argument('--font', help='Optional Fcitx font, e.g. "Sans 12"')
    parser.add_argument('--tray-icon', choices=('image', 'text'),
                        help='Optional shared Classic UI tray icon preference')
    parser.add_argument('--mode-hint', choices=('compact', 'full'),
                        help='Optional shared Fcitx switching hint style')
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
        backup = apply_theme(args.mode, path, args.font, args.dry_run, args.tray_icon)
        hint_backup = None
        if args.mode_hint:
            if args.dry_run:
                print('# fcitx5/config')
            hint_backup = apply_mode_hint(config / 'fcitx5/config', args.mode_hint, args.dry_run)
    except (OSError, ValueError) as error:
        parser.exit(1, f'ari-ime-theme: {error}\n')
    if args.dry_run:
        return
    print('Classic UI theme:', args.mode)
    print('This applies to all input methods using Fcitx Classic UI.')
    if args.tray_icon:
        print('Shared tray icon preference:', args.tray_icon)
        print('Switch input methods or restart Fcitx5 to refresh the tray icon.')
    if backup:
        print('Previous configuration:', backup)
    if args.mode_hint:
        print('Shared input-method switching hint:', args.mode_hint)
    if hint_backup:
        print('Previous global configuration:', hint_backup)
    if args.no_reload:
        print('Configuration saved; live theme was not reloaded.')
    else:
        if args.mode_hint:
            try:
                controller_call('ReloadConfig')
            except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
                print(f'Global configuration saved, but reload failed: {error}', file=sys.stderr)
                return 1
        if not reload_theme():
            return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
