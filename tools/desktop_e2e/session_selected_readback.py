"""Read only selected-tab metadata from owned synthetic SNSS3; not a native decoder."""
import hashlib
import json
import pathlib
import struct
import sys


def inspect(file):
    data = file.read_bytes()
    assert data[:4] == b'SNSS' and struct.unpack_from('<I', data, 4)[0] == 3
    offset = 8
    selected, indices, windows, titles = {}, {}, {}, {}
    sequence = []
    commands = 0
    while offset < len(data):
        size = struct.unpack_from('<H', data, offset)[0]
        offset += 2
        assert size >= 1 and offset + size <= len(data)
        command = data[offset]
        body = data[offset + 1:offset + size]
        offset += size
        commands += 1
        if command in [0, 2, 8]:
            assert len(body) == 8
            identifier, value = struct.unpack('<ii', body)
            if command == 0:
                windows[value] = identifier
            elif command == 2:
                indices[identifier] = value
            else:
                selected[identifier] = value
                sequence.append(dict(window=identifier, index=value))
        elif command == 6:
            # Pickle header, tab id, navigation index, URL string, title16.
            # Read just this bounded prefix; never export page-state/URL data.
            assert len(body) >= 16
            tab, navigation, length = struct.unpack_from('<iii', body, 4)
            assert 0 <= length <= len(body) - 16
            position = 16 + ((length + 3) // 4) * 4
            count = struct.unpack_from('<i', body, position)[0]
            assert 0 <= count and position + 4 + count * 2 <= len(body)
            title = body[position + 4:position + 4 + count * 2].decode('utf-16-le')
            # Only the known synthetic fixture labels leave this reader.
            if title in ['Solo'] + ['Pane' + letter for letter in 'ABCDEFGH']:
                titles[tab] = title
        elif command == 16:
            identifier = struct.unpack_from('<i', body)[0]
            windows.pop(identifier, None)
            indices.pop(identifier, None)
            titles.pop(identifier, None)
        elif command == 17:
            selected.pop(struct.unpack_from('<i', body)[0], None)
    return dict(file=file.name, sha256=hashlib.sha256(data).hexdigest(), bytes=len(data),
                parsedBytes=offset, commands=commands, partialMetadataOnly=True,
                selected=selected, selectionSequence=sequence,
                selectedFixtureTitles={window: [titles.get(tab, 'non-fixture')
                    for tab, index in indices.items() if index == value and windows.get(tab) == window]
                    for window, value in selected.items()})


if __name__ == '__main__':
    profile = pathlib.Path(sys.argv[1]).resolve()
    assert profile.parent == pathlib.Path('/private/tmp') and profile.name.startswith('ahoi-split-profile.')
    print(json.dumps([inspect(file) for file in sorted((profile / 'Default/Sessions').glob('Session_*'))], indent=2))
