# Copyright 2020-2026 The Defold Foundation
# Copyright 2014-2020 King
# Copyright 2009-2014 Ragnar Svensson, Christian Murray
# SPDX-License-Identifier: MIT

"""Check editor YAML against runtime exports, and compile every documented example."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import yaml

path, executable = Path(sys.argv[1]), sys.argv[2]
modules = yaml.safe_load(path.read_text())
assert len(modules) == 1 and modules[0]['name'] == 'messagepack'
assert modules[0]['type'] == 'table' and modules[0]['desc']
members = modules[0]['members']
documented = {member['name']: member['type'] for member in members}
assert len(documented) == len(members), 'Duplicate documented API member'
actual = dict(line.split() for line in subprocess.check_output([executable, '--api'], text=True).splitlines())
assert documented == actual, (documented, actual)
examples = 0
with tempfile.TemporaryDirectory(prefix='messagepack-api-') as folder:
    for member in members:
        assert member['desc'], member['name']
        if member['type'] == 'function':
            for parameter in member.get('parameters', []):
                assert parameter.get('type') and parameter.get('desc')
            for result in member.get('returns', []):
                assert result.get('type') and result.get('desc')
        for example in member.get('examples', []):
            code = re.search(r'```lua\n(.*?)```', example['desc'], re.S)
            assert code, 'Missing Lua example'
            source = Path(folder) / f'example-{examples}.lua'
            source.write_text(code[1])
            subprocess.run([executable, '--check-lua', str(source)], check=True)
            examples += 1
assert examples > 0
print(f'API documentation matches {len(actual)} runtime exports; {examples} Lua examples compile.')
