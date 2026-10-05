"""Verify the exact install payload, checksum, x64 PE exports and test provenance."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zipfile


def verify_pe(data):
    assert data[:2] == b'MZ', 'Missing DOS header'
    offset = struct.unpack_from('<I', data, 0x3c)[0]
    assert data[offset:offset + 4] == b'PE\0\0', 'Missing PE header'
    machine, sections = struct.unpack_from('<HH', data, offset + 4)
    optional_size, flags = struct.unpack_from('<HH', data, offset + 20)
    assert machine == 0x8664 and flags & 0x2000, 'Expected x64 DLL'
    optional = offset + 24
    assert struct.unpack_from('<H', data, optional)[0] == 0x20b, 'Expected PE32+'
    section_base = optional + optional_size
    mappings = []
    for index in range(sections):
        virtual_size, rva, raw_size, raw = struct.unpack_from('<IIII', data, section_base + index * 40 + 8)
        mappings.append((rva, max(virtual_size, raw_size), raw))
    def file_offset(rva):
        for start, size, raw in mappings:
            if start <= rva < start + size:
                return raw + rva - start
        raise AssertionError('Unmapped PE address')
    def cstring(rva):
        start = file_offset(rva)
        return data[start:data.index(0, start)].decode('ascii')
    export_rva = struct.unpack_from('<I', data, optional + 112)[0]
    table = file_offset(export_rva)
    count = struct.unpack_from('<I', data, table + 24)[0]
    names_rva = struct.unpack_from('<I', data, table + 32)[0]
    names = {cstring(struct.unpack_from('<I', data, file_offset(names_rva) + index * 4)[0]) for index in range(count)}
    assert {'NDD_PROC_IDENTIFY', 'NDD_PROC_MAIN'} <= names, 'Plugin entry exports missing'
    imports = []
    import_rva = struct.unpack_from('<I', data, optional + 120)[0]
    if import_rva:
        descriptor = file_offset(import_rva)
        while any(data[descriptor:descriptor + 20]):
            imports.append(cstring(struct.unpack_from('<I', data, descriptor + 12)[0]))
            descriptor += 20
    assert not any(name.lower().endswith(('cored.dll', 'guid.dll', 'widgetsd.dll', 'ucrtbased.dll', 'vcruntime140d.dll')) for name in imports), 'Debug runtime dependency'
    assert not any('qscintilla' in name.lower() or 'qmyedit' in name.lower() for name in imports), 'Plugin must not import a separate QScintilla DLL'
    return {'architecture': 'PE32+ x86-64', 'exports': sorted(names), 'imports': imports}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('archive', type=Path)
    parser.add_argument('--commit', required=True)
    args = parser.parse_args()
    digest = hashlib.sha256(args.archive.read_bytes()).hexdigest()
    checksum, filename = Path(str(args.archive) + '.sha256').read_text(encoding='utf-8-sig').split()
    assert checksum == digest and filename == args.archive.name, 'ZIP checksum mismatch'
    with zipfile.ZipFile(args.archive) as package:
        files = {name.replace('\\', '/') for name in package.namelist() if not name.endswith('/')}
        expected = {'plugin/calctabdd.dll', 'README.md', 'LICENSE', 'TESTING.md', 'CHANGELOG.md', 'BUILD-INFO.json'}
        assert files == expected, f'Unexpected package files: {files ^ expected}'
        lookup = {name.replace('\\', '/'): name for name in package.namelist()}
        info = json.loads(package.read(lookup['BUILD-INFO.json']).decode('utf-8-sig'))
        assert info['commit'] == args.commit, 'Package source commit mismatch'
        assert info['qt'] == '5.15.2' and info['architecture'] == 'x64'
        assert info['host_source'] == '91105f68b74382128f3313ac5af8accdc77de918'
        assert set(info['tests']) == {'engine_tests', 'page_tests', 'plugin_tests'}
        for name, result in info['tests'].items():
            assert result['passed'] > 2 and result['failed'] == 0 and result['skipped'] == 0, f'Incomplete suite: {name}'
        host_checks = info['released_host_tests']
        assert set(host_checks) == {'3.8.3', '3.9.0'}, 'Official host versions missing'
        required_checks = {'host_created_native_tab', 'read_existing_unicode_multiline',
                           'read_readonly_selection', 'production_menu_installed',
                           'production_dll_preview', 'selection_preserved',
                           'clipboard_preserved', 'empty_selection', 'source_preserved'}
        for version, result in host_checks.items():
            assert result['passed'] is True and result['qt'] == '5.15.2', f'Host {version} failed'
            assert required_checks <= set(result['checks']), f'Host {version} checks incomplete'
        pe = verify_pe(package.read(lookup['plugin/calctabdd.dll']))
    print(json.dumps({'file': args.archive.name, 'sha256': digest, 'build': info, 'pe': pe}, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
