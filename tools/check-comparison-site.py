"""Validate the portable static site before GitHub Pages publication; CPU only."""
from pathlib import Path
from html.parser import HTMLParser
from urllib.parse import urlsplit,unquote
import hashlib,json,sys
root=Path(__file__).resolve().parents[1]/(sys.argv[1] if len(sys.argv)>1 else 'docs')
class Links(HTMLParser):
    def __init__(self):super().__init__();self.targets=[]
    def handle_starttag(self,tag,attrs):
        for key,value in attrs:
            if key in ('src','href') and value:self.targets.append(value)
for page in root.glob('*.html'):
    content=page.read_text(encoding='utf-8')
    parser=Links();parser.feed(content)
    for link in parser.targets:
        url=urlsplit(link)
        if url.scheme or not url.path:continue
        assert not url.path.startswith('/'),link+' breaks project-relative hosting'
        target=(page.parent/unquote(url.path)).resolve()
        assert target.is_relative_to(root.resolve()) and target.exists(),link
provenance=json.loads((root/'assets/provenance.json').read_text(encoding='utf-8'))
for image in provenance['images']:
    p=root/'assets'/image['file']
    assert hashlib.sha256(p.read_bytes()).hexdigest()==image['sha256'],str(p)
comparisons=json.loads((root/'assets/comparisons.json').read_text(encoding='utf-8'))
assert set(comparisons['scenes'])=={'cab','exterior'}
for scene in comparisons['scenes'].values():
    assert (scene['width'],scene['height'])==(2504,2600)
    expected={f'{q}-{s}-{l}' for q in ('low','medium','high','ultra') for s in ('natural','default','cinematic') for l in ('clean','cooler','cold')}
    assert set(scene['samples'])==expected
    names=list(scene['original'].values())
    for sample in scene['samples'].values():names.extend(sample.values())
    for name in names:
        assert name.startswith('assets/') and (root/name).is_file(),name
assert len(provenance['images'])==148 and len(provenance['runs'])==24
assert len({i['file'] for i in provenance['images']})==148
assert all(r['verified'] for r in provenance['runs'])
assert not any(':/Users/' in str(v) for v in provenance.values())
print('Verified 72 results, two originals, 74 smaller previews, all hashes and local page links.')
