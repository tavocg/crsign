#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate a per-user WiX 4 installer from the tested portable Windows tree."""
import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import uuid
import xml.etree.ElementTree as ET

NS = 'http://wixtoolset.org/schemas/v4/wxs'
UPGRADE = uuid.UUID('f60c16fb-0bfe-4aa8-8a61-07d117f844c2')


def identifier(prefix, value):
    return prefix + hashlib.sha256(value.casefold().encode()).hexdigest()[:24]


def author(payload, version):
    if not re.fullmatch(r'\d+\.\d+\.\d+', version):
        raise ValueError('MSI version must be major.minor.patch')
    if any(n > limit for n, limit in zip(map(int, version.split('.')), (255, 255, 65535))):
        raise ValueError('MSI version exceeds Windows Installer limits')
    if not (payload / 'bin/sign.exe').is_file():
        raise ValueError('Missing staged bin/sign.exe')
    ET.register_namespace('', NS)
    def node(parent, tag, **attributes):
        return ET.SubElement(parent, f'{{{NS}}}{tag}', attributes)
    root = ET.Element(f'{{{NS}}}Wix')
    package = node(root, 'Package', Name='CRSign', Manufacturer='CRSign', Version=version,
                   Language='1033', UpgradeCode=str(UPGRADE), Scope='perUser', InstallerVersion='500')
    node(package, 'MajorUpgrade', DowngradeErrorMessage='A newer CRSign is already installed.',
         AllowSameVersionUpgrades='yes')
    node(package, 'MediaTemplate', EmbedCab='yes', CompressionLevel='high')
    icon = payload / 'share/crsign/icon.ico'
    if icon.is_file():
        node(package, 'Icon', Id='CRSignIcon', SourceFile=str(icon.resolve()))
        node(package, 'Property', Id='ARPPRODUCTICON', Value='CRSignIcon')
    local = node(package, 'StandardDirectory', Id='LocalAppDataFolder')
    programs = node(local, 'Directory', Id='UserPrograms', Name='Programs')
    install = node(programs, 'Directory', Id='INSTALLFOLDER', Name='CRSign')
    directories = {'.': install}
    feature = node(package, 'Feature', Id='Main', Title='CRSign', Level='1')
    main_file = None
    for path in sorted(payload.rglob('*')):
        if not path.is_file():
            continue
        relative = path.relative_to(payload)
        directory = install
        for parent in reversed(relative.parents):
            key = parent.as_posix()
            if key == '.':
                continue
            if key not in directories:
                directories[key] = node(directory, 'Directory', Id=identifier('D', key), Name=parent.name)
            directory = directories[key]
        key = relative.as_posix()
        component_id, file_id = identifier('C', key), identifier('F', key)
        component = node(directory, 'Component', Id=component_id,
                         Guid=str(uuid.uuid5(UPGRADE, 'x86_64/' + key.casefold())), Bitness='always64')
        node(component, 'File', Id=file_id, Source=str(path.resolve()), Name=path.name)
        node(component, 'RegistryValue', Root='HKCU', Key='Software\\CRSign\\Installer\\Files',
             Name=component_id, Type='string', Value=version, KeyPath='yes')
        node(feature, 'ComponentRef', Id=component_id)
        if key == 'bin/sign.exe':
            main_file = file_id
    menu = node(package, 'StandardDirectory', Id='ProgramMenuFolder')
    folder = node(menu, 'Directory', Id='CRSignMenu', Name='CRSign')
    shortcut = node(folder, 'Component', Id='MenuShortcut', Guid=str(uuid.uuid5(UPGRADE, 'shortcut')),
                    Bitness='always64')
    node(shortcut, 'Shortcut', Id='StartMenuShortcut', Name='CRSign', Target=f'[#{main_file}]',
         Arguments='--gui', WorkingDirectory='INSTALLFOLDER', **({'Icon': 'CRSignIcon', 'IconIndex': '0'} if icon.is_file() else {}))
    node(shortcut, 'RemoveFolder', Id='RemoveCRSignMenu', On='uninstall')
    for directory in [programs, *directories.values()]:
        identity = directory.attrib['Id']
        node(shortcut, 'RemoveFolder', Id=identifier('R', identity),
             Directory=identity, On='uninstall')
    node(shortcut, 'RegistryValue', Root='HKCU', Key='Software\\CRSign\\Installer',
         Name='Shortcut', Type='integer', Value='1', KeyPath='yes')
    node(feature, 'ComponentRef', Id='MenuShortcut')
    ET.indent(root)
    return ET.ElementTree(root)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--payload', type=Path, required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    source = args.output.with_suffix('.wxs')
    author(args.payload.resolve(), args.version).write(source, encoding='utf-8', xml_declaration=True)
    subprocess.run(['wix', 'build', '-arch', 'x64', '-o', str(args.output), str(source)], check=True)


if __name__ == '__main__':
    main()
