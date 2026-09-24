"""Extract the pinned official PebbleOS PBFs. Usage: python extract-fonts.py PebbleOS"""
import sys, json, subprocess, hashlib
from pathlib import Path
root=Path(sys.argv[1]).resolve()
sys.path.insert(0,str(root/'tools/font'))
from pbf_extract import extract_pbf
from PIL import Image
out={}
rev=subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip()
for name in ['LECO_42_NUMBERS','LECO_20_BOLD_NUMBERS']:
    source=root/'resources/normal/base/pbf'/f'{name}.pbf'
    dest=Path('build/fonts')/name
    meta=extract_pbf(str(source),str(dest))
    glyphs={}
    for g in meta['glyphs']:
        if chr(g['codepoint']) not in '0123456789:.':continue
        image=Image.open(dest/g['file']).convert('L')
        glyphs[chr(g['codepoint'])]={**{k:g[k] for k in ['width','height','left_offset','top_offset','advance']},'bits':''.join('1' if v<128 else '0' for v in image.getdata())}
    out[name]={'glyphs':glyphs,'height':meta['max_height'],'sha256':hashlib.sha256(source.read_bytes()).hexdigest()}
out['source']={'repository':'https://github.com/coredevices/PebbleOS','revision':rev,'license':'Apache-2.0','path':'resources/normal/base/pbf'}
Path('public/fonts/leco.json').write_text(json.dumps(out))
