#!/usr/bin/env python3
"""Protect user Classic UI configuration when explicitly applying Ari themes."""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'scripts/apply-theme.py'
spec = importlib.util.spec_from_file_location('apply_theme', SCRIPT)
theme = importlib.util.module_from_spec(spec)
spec.loader.exec_module(theme)


class ThemeTests(unittest.TestCase):
    def test_live_reload_targets_addon_and_checks_active_renderer(self):
        replies = [subprocess.CompletedProcess([], 0, '', ''),
                   subprocess.CompletedProcess([], 0, 's "classicui"\n', '')]
        with patch.object(theme.shutil, 'which', return_value='/usr/bin/busctl'), \
             patch.object(theme.subprocess, 'run', side_effect=replies) as run:
            self.assertTrue(theme.reload_theme())
        command = run.call_args_list[0].args[0]
        self.assertEqual(command[-3:], ['ReloadAddonConfig', 's', 'classicui'])
        self.assertEqual(run.call_args_list[1].args[0][-1], 'CurrentUI')

    def test_gdbus_fallback_parses_renderer(self):
        with patch.object(theme.shutil, 'which', side_effect=[None, '/usr/bin/gdbus']), \
             patch.object(theme.subprocess, 'run', return_value=
                          subprocess.CompletedProcess([], 0, "('classicui',)\n", '')):
            self.assertEqual(theme.controller_call('CurrentUI'), 'classicui')

    def test_external_renderer_and_reload_failure_are_not_success(self):
        with patch.object(theme, 'controller_call', side_effect=['', 'kimpanel']):
            self.assertFalse(theme.reload_theme())
        with patch.object(theme, 'controller_call', side_effect=RuntimeError('No session bus')):
            self.assertFalse(theme.reload_theme())
        with patch.object(theme, 'controller_call', side_effect=['', RuntimeError('Unknown method')]):
            self.assertFalse(theme.reload_theme())

    def test_preserves_other_keys_comments_and_sections(self):
        original = '# 我的設定\nFont=Sans 15\nTheme=old\nTheme=duplicate\nWheelForPaging=False\n[Other]\nTheme=keep\n'
        updated = theme.update_root_options(original, {'Theme': 'ari-dark', 'DarkTheme': 'ari-dark'})
        self.assertEqual(updated, '# 我的設定\nFont=Sans 15\nTheme=ari-dark\nWheelForPaging=False\nDarkTheme=ari-dark\n[Other]\nTheme=keep\n')

    def test_backup_is_exact_and_second_apply_is_idempotent(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'classicui.conf'
            original = '# keep\nFont=Sans 14\nTheme=old'
            path.write_text(original)
            backup = theme.apply_theme('dark', path)
            self.assertEqual(backup.read_text(), original)
            self.assertIn('Font=Sans 14', path.read_text())
            self.assertIn('Theme=ari-dark', path.read_text())
            self.assertIn('UseDarkTheme=False', path.read_text())
            self.assertIsNone(theme.apply_theme('dark', path))
            self.assertEqual(len(list(path.parent.glob('*.ari-backup-*'))), 1)

    def test_auto_uses_distinct_themes_and_preserves_font(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'classicui.conf'
            theme.apply_theme('auto', path, 'Sans 12')
            self.assertEqual(path.read_text(), 'Theme=ari-light\nDarkTheme=ari-dark\nUseDarkTheme=True\nFont=Sans 12\n')

    def test_tray_icon_preference_changes_only_when_requested(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'classicui.conf'
            path.write_text('PreferTextIcon=True\nTrayFont=Sans Bold 11\n')
            theme.apply_theme('dark', path)
            self.assertIn('PreferTextIcon=True', path.read_text())
            theme.apply_theme('dark', path, tray_icon='image')
            self.assertIn('PreferTextIcon=False', path.read_text())
            self.assertIn('TrayFont=Sans Bold 11', path.read_text())
            theme.apply_theme('dark', path, tray_icon='text')
            self.assertIn('PreferTextIcon=True', path.read_text())

    def test_dry_run_and_invalid_font_do_not_write(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'missing' / 'classicui.conf'
            from contextlib import redirect_stdout
            from io import StringIO
            with redirect_stdout(StringIO()):
                theme.apply_theme('dark', path, dry_run=True)
            self.assertFalse(path.parent.exists())
            with self.assertRaises(ValueError):
                theme.apply_theme('dark', path, 'Sans 12\nTheme=bad')
            self.assertFalse(path.exists())

    def test_symlink_keeps_dotfile_link(self):
        with tempfile.TemporaryDirectory() as folder:
            target = Path(folder) / 'dotfile'
            target.write_text('Theme=old\n')
            link = Path(folder) / 'classicui.conf'
            link.symlink_to(target)
            theme.apply_theme('light', link)
            self.assertTrue(link.is_symlink())
            self.assertIn('Theme=ari-light', target.read_text())

    def test_empty_xdg_uses_home_defaults(self):
        with tempfile.TemporaryDirectory() as folder:
            installed = Path(folder) / '.local/share/fcitx5/themes/ari-dark'
            installed.mkdir(parents=True)
            (installed / 'theme.conf').write_text('[Metadata]\nName=Ari Dark\n')
            env = dict(os.environ, HOME=folder, XDG_CONFIG_HOME='',
                       XDG_DATA_HOME='', XDG_DATA_DIRS='')
            result = subprocess.run([sys.executable, str(SCRIPT), 'dark', '--no-reload'],
                                    env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('Theme=ari-dark', (Path(folder) / '.config/fcitx5/conf/classicui.conf').read_text())

    def test_cli_requires_installed_theme_before_writing(self):
        with tempfile.TemporaryDirectory() as folder:
            env = dict(os.environ, XDG_CONFIG_HOME=folder + '/config',
                       XDG_DATA_HOME=folder + '/data', XDG_DATA_DIRS=folder + '/system')
            result = subprocess.run([sys.executable, str(SCRIPT), 'dark', '--no-reload'],
                                    env=env, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((Path(folder) / 'config').exists())
            installed = Path(folder) / 'data/fcitx5/themes/ari-dark'
            installed.mkdir(parents=True)
            (installed / 'theme.conf').write_text('[Metadata]\nName=Ari Dark\n')
            result = subprocess.run([sys.executable, str(SCRIPT), 'dark', '--no-reload',
                                     '--tray-icon', 'image'],
                                    env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            saved = (Path(folder) / 'config/fcitx5/conf/classicui.conf').read_text()
            self.assertIn('Theme=ari-dark', saved)
            self.assertIn('PreferTextIcon=False', saved)


if __name__ == '__main__':
    unittest.main(verbosity=2)
