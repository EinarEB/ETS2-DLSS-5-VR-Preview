"""Create a candidate from an explicit source/payload list. No game or downloads are read."""
from pathlib import Path
import argparse, datetime, hashlib, json, re, shutil, zipfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = '0.1'
DEPENDENCIES = {
    'eurotrucks2.exe': '03a0a051ecaddf95a4271e014e13c15ddddc2381c094278672a84d37206aa49f',
    'ReShade64.dll': '0cee63f9c9f13f3ac909c5b4903f4dbb4b719a7ab3b4f13b0deaf83c814b94f7',
    'snowymoon-dxgi.dll': '1bc3e4d8c270ce3843f131d16e42aa39c03f7ff10634ca5a2a7db3a3d28fcacb',
    'renodx-dlss5.addon64': '87aef9ddd937c7241e6bf8d8efea0045d63559135e254c60dab316db3d3a4aee',
    'nvngx_dlss.dll': 'c85f971ce023c9f3492fc7455f0b01a24ba18ea39636407a846902c4360b0b7e',
    'nvngx_dlssnr.dll': 'e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e',
}
BINARY_FILES = {
    'build/feeder/dlss5-feed.addon64': 'dlss5-feed.addon64',
    'build/depth/ets2-stereo-depth.addon64': 'ets2-stereo-depth.addon64',
    'build/monitor/ets2-monitor.addon64': 'ets2-monitor.addon64',
    'build/ui/ETS2 VR Preview.exe': 'ETS2 VR Preview.exe',
}
TEMPLATES = [
    'dlss5-feed.cfg','preset-desktop.ini','ReShade.ini','ReShadeVR.ini',
    'Reshade presets/ETS2_VR_Preview_Clean.ini',
    'Reshade presets/ETS2_VR_Preview_Cooler.ini',
    'Reshade presets/ETS2_VR_Preview_ColdGrade.ini',
    'game-root/bin/win_x64/snowymoon.io_lighting_v1_0_0.cfg',
]
SHADERS = [
    'DLSS5_Feed.fx','ETS2_VortStereo.fx','ETS2_VortStereo_Eye.fxh','vort_BlueNoise.png',
    'ReShade.fxh','PD80_04_Color_Temperature.fx','PD80_04_Contrast_Brightness_Saturation.fx',
    'PD80_00_Color_Spaces.fxh','PD80_00_Noise_Samplers.fxh','PD80_00_Blend_Modes.fxh',
    'PD80_00_Base_Effects.fxh','pd80_bluenoise.png','pd80_bluenoise_rgba.png','pd80_gaussnoise.png',
]
SOURCE_DIRS = {'src','tests','templates','shaders','external','licenses','tools','assets','docs','site','.github'}
SOURCE_ROOT_FILES = {'README.md','LICENSE','THIRD-PARTY-NOTICES.md','CHANGELOG.md','release-version.txt',
                     'Read me first.html','build.cmd','test.cmd','source-dependencies.json','.gitignore','.gitattributes'}
ALLOWED_EXTENSIONS = {'.cpp','.h','.hpp','.c','.rc','.cs','.fx','.fxh','.ini','.cfg','.json','.txt','.md',
                      '.png','.svg','.ico','.manifest','.py','.cmd','.html','.css','.js','.yml','.yaml',''}

def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda:stream.read(1024*1024),b''):h.update(chunk)
    return h.hexdigest()

def require(path):
    if not path.is_file() or path.is_symlink() or path.resolve() != path.absolute():
        raise ValueError('Missing or redirected package input: '+str(path.relative_to(ROOT)))
    return path

def copy(source,target):
    require(source);target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(source,target)
    if sha(source)!=sha(target):raise ValueError('Copy verification failed: '+target.name)

def source_files():
    found=[]
    for file in sorted(ROOT.rglob('*')):
        relative=file.relative_to(ROOT)
        if relative.parts[0] not in SOURCE_DIRS and relative.as_posix() not in SOURCE_ROOT_FILES:continue
        if file.is_dir():continue
        # The native comparison photos belong to the website. Keep hundreds of
        # megabytes of gallery images out of the installer and source bundle.
        if relative.parts[:2] == ('docs','assets') and file.suffix.lower()=='.webp':continue
        require(file)
        if file.suffix.lower() not in ALLOWED_EXTENSIONS:raise ValueError('Unapproved source extension: '+str(relative))
        if file.name in DEPENDENCIES or file.name=='preview.json':raise ValueError('Private/runtime input in source: '+file.name)
        if file.stat().st_size>5*1024*1024:raise ValueError('Unexpectedly large source file: '+str(relative))
        found.append(file)
    return found

def audit_text(files):
    needles=[b'github_pat_',b'ghp_',b'Bearer ']
    for file in files:
        if file.suffix.lower() in ('.png','.ico'):continue
        data=file.read_bytes()
        # This script describes the audit needles itself; do not classify its
        # own literal detector strings as leaked source data.
        if file==Path(__file__).resolve():continue
        for needle in needles:
            if needle.lower() in data.lower():raise ValueError('Review private/path/token pattern in '+file.relative_to(ROOT).as_posix())
        if re.search(rb'[a-zA-Z]:[\\/]+Users[\\/]+[^\\/\r\n]+[\\/]',data,re.I):
            raise ValueError('Review local user-home path in '+file.relative_to(ROOT).as_posix())
        # One explicitly supplied public download is documented with its expiry.
        # Other attachment URLs need an independent privacy review.
        for attachment in re.findall(rb'https://cdn\.discordapp\.com/attachments/[^\s<>"\)]+',data):
            allowed=b'https://cdn.discordapp.com/attachments/1545049227321810974/1545050050609025114/DLSS310.8.0-Streamline2.13.zip'
            if not attachment.startswith(allowed+b'?') and attachment!=allowed:
                raise ValueError('Unreviewed attachment URL in '+file.relative_to(ROOT).as_posix())

def archive(folder,target,prefix):
    with zipfile.ZipFile(target,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for file in sorted(folder.rglob('*')):
            if file.is_file():z.write(file,prefix+'/'+file.relative_to(folder).as_posix())

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--label',default=VERSION);args=parser.parse_args()
    if not args.label or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._' for c in args.label):raise ValueError('Use a simple version label')
    stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d-%H%M%S')
    out=ROOT/'dist'/(args.label+'-'+stamp);out.mkdir(parents=True,exist_ok=False)
    stage=out/'release';payload=stage/'payload';payload.mkdir(parents=True)
    source=source_files();audit_text(source)
    pins=json.loads((ROOT/'source-dependencies.json').read_text(encoding='utf-8'))
    for dep in pins['dependencies']:
        for file in dep['files']:
            if sha(require(ROOT/file['path']))!=file['sha256']:raise ValueError('Vendored source changed: '+file['path'])
    for local,target in BINARY_FILES.items():copy(ROOT/local,payload/target)
    copy(ROOT/'build/ui/Setup ETS2 VR Preview.exe',stage/'Setup ETS2 VR Preview.exe')
    for name in TEMPLATES:copy(ROOT/'templates'/name,payload/name)
    for name in SHADERS:copy(ROOT/'shaders'/name,payload/'fx'/name)
    documentation=[ROOT/name for name in ['Read me first.html','README.md','THIRD-PARTY-NOTICES.md','CHANGELOG.md']]
    documentation += [p for p in source if p.relative_to(ROOT).parts[0]=='licenses' or
                      (p.relative_to(ROOT).parts[0]=='docs' and p.relative_to(ROOT).parts[1]!='assets' and
                       p.suffix.lower() in {'.md','.json','.png','.svg'} and p.relative_to(ROOT).as_posix()!='docs/README.md')]
    for file in documentation:
        copy(file,stage/file.relative_to(ROOT));copy(file,payload/file.relative_to(ROOT))
    required=stage/'Required files';required.mkdir()
    (required/'Add your downloads here.txt').write_text('Open Read me first.html one folder above. Add your own five required files here.\nDo not put credentials or save files in this folder.\n',encoding='utf-8')
    manifest={'schema':1,'version':args.label,'headset_validated':False,'dependencies':DEPENDENCIES,
              'payload':{p.relative_to(payload).as_posix():sha(p) for p in sorted(payload.rglob('*')) if p.is_file()}}
    (stage/'package-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    provenance={'schema':1,'version':args.label,'created_utc':stamp,'headset_validated':False,'published':False,
                'toolchain':(ROOT/'build/toolchain.txt').read_text(encoding='utf-8-sig').splitlines(),
                'source':{p.relative_to(ROOT).as_posix():sha(p) for p in source},
                'binaries':{p:sha(ROOT/p) for p in list(BINARY_FILES)+['build/ui/Setup ETS2 VR Preview.exe']}}
    (stage/'build-manifest.json').write_text(json.dumps(provenance,indent=2)+'\n',encoding='utf-8')
    (stage/'SHA256SUMS.txt').write_text(''.join(sha(p)+'  '+p.relative_to(stage).as_posix()+'\n' for p in sorted(stage.rglob('*')) if p.is_file()),encoding='utf-8')
    release_zip=out/('ETS2-DLSS-5-VR-Preview-'+args.label+'-win-x64.zip')
    archive(stage,release_zip,'ETS2 DLSS 5 VR Preview')
    source_zip=out/('ETS2-DLSS-5-VR-Preview-'+args.label+'-source.zip')
    with zipfile.ZipFile(source_zip,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for file in source:z.write(file,'ETS2-DLSS-5-VR-Preview/'+file.relative_to(ROOT).as_posix())
    (out/'SHA256SUMS.txt').write_text(''.join(sha(p)+'  '+p.name+'\n' for p in [release_zip,source_zip]),encoding='utf-8')
    print(json.dumps({'output':str(out),'payload_files':len(manifest['payload']),'source_files':len(source),'release_bytes':release_zip.stat().st_size,'published':False},indent=2))

if __name__=='__main__':main()
