"""Build the offline, self-contained flowchart editor from local sources."""
import json
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE
source = (ROOT / 'diagrams.md').read_text()
blocks = re.findall(r'^##\s+(\d+)\.\s+(.+?)\n+```mermaid\n(.*?)\n```', source, re.M | re.S)
data = [dict(id=int(i), title=title, source=body) for i, title, body in blocks]
assert data
template = (HERE / 'template.html').read_text()
script = (HERE / 'editor.js').read_text()
result = template.replace('/*__DIAGRAMS__*/', json.dumps(data, ensure_ascii=False).replace('</', '<\\/'))
result = result.replace('/*__EDITOR__*/', script)
(ROOT / 'index.html').write_text(result)
print(f'Built {len(data)} diagrams; {len(result.encode())} bytes')
