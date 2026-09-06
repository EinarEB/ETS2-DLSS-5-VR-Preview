"""One-time transfer of already-public images, with pinned pixel-asset hashes.

Removed after migration; the deployed page uses only repository-local assets.
"""
from pathlib import Path
from urllib.request import Request,urlopen
import hashlib,json
root=Path(__file__).resolve().parents[1]
assets=root/'site/assets'
manifest=json.loads((assets/'provenance.json').read_text(encoding='utf-8'))
base='https://ets2-dlss5-vr-preview.einareb.chatgpt.site/assets/'
for image in manifest['images']:
    name=image['file']
    assert Path(name).name==name and name.endswith(('.png','.webp'))
    target=assets/name
    if target.exists() and hashlib.sha256(target.read_bytes()).hexdigest()==image['sha256']:
        continue
    with urlopen(Request(base+name,headers={'User-Agent':'ETS2-VR-Preview-migration'}),timeout=120) as response:
        data=response.read(20*1024*1024+1)
    assert len(data)<=20*1024*1024
    assert hashlib.sha256(data).hexdigest()==image['sha256'],name+' differs from the verified public capture'
    target.write_bytes(data)
    print('Verified',name,len(data))
