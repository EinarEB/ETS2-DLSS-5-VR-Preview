"""Record successful CPU checks without publishing local fixture paths."""
from pathlib import Path
import datetime,hashlib,json,re
root=Path(__file__).resolve().parents[1]
def count(path,pattern):
 text=(root/path).read_text(encoding='utf-8',errors='replace');match=re.search(pattern,text)
 if not match:raise RuntimeError('Missing successful check result: '+path)
 return int(match.group(1))
record={'version':'0.1','recorded_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'game_launched':False,
 'setup_checks':count('build/tests/setup/output.txt',r'Passed (\d+) CPU/file checks'),
 'requirement_checks':count('build/tests/setup/requirements-output.txt',r'Passed (\d+) requirement checks'),
 'launcher_checks':count('build/tests/launcher/output.txt',r'Checks: (\d+); failed: 0;'),
 'comparison_controller':json.loads((root/'build/tests/blend/results.json').read_text()),
 'source_sha256':{str(p.relative_to(root)).replace('\\','/'):hashlib.sha256(p.read_bytes()).hexdigest() for folder in ('src/launcher','tests') for p in sorted((root/folder).glob('*')) if p.is_file()}}
(root/'docs/CPU-CHECKS.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
print('Recorded release CPU checks. No game or headset test claimed.')
