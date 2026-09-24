"""Compare extracted PBF glyph pixels against an emulator screenshot."""
import json,sys
from PIL import Image
fonts=json.load(open('public/fonts/leco.json'))
im=Image.open(sys.argv[1]).convert('RGB')
expected=set()
for text,name,top in [(sys.argv[2],'LECO_42_NUMBERS',78),(sys.argv[3],'LECO_20_BOLD_NUMBERS',128)]:
    glyphs=fonts[name]['glyphs']
    x=(200-sum(glyphs[c]['advance'] for c in text))//2
    for c in text:
        g=glyphs[c]
        for y in range(g['height']):
            for xx in range(g['width']):
                if g['bits'][y*g['width']+xx]=='1':expected.add((x+g['left_offset']+xx,top+g['top_offset']+y))
        x+=g['advance']
actual={(x,y) for y in range(228) for x in range(200) if im.getpixel((x,y))==(255,255,255)}
print(json.dumps({'expectedPixels':len(expected),'actualPixels':len(actual),'missing':len(expected-actual),'extra':len(actual-expected)}))
assert expected==actual
