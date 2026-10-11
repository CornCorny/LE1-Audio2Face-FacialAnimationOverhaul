"""Contact sheets for inspection of GPU-rendered frames; no animation compute."""
from pathlib import Path
import json
from PIL import Image,ImageDraw
root=Path(__file__).resolve().parents[2];p=root/'research/intro-a2f/correction-0.2.6'
meta=json.loads((p/'motion/render.json').read_text());dest=p/'review';dest.mkdir(exist_ok=True)
for actor in ['Kaidan','Joker','Shepard']:
    frames=[x for x in meta['Frames'] if x['Actor']==actor and x['Version']=='old']
    ids=sorted(set(round(i*(len(frames)-1)/11) for i in range(12)))
    sheet=Image.new('RGB',(960,4*190),(24,24,24));draw=ImageDraw.Draw(sheet)
    for j,i in enumerate(ids):
        x=(j%3)*320;y=(j//3)*190
        draw.text((x+3,y+2),f'{actor} {frames[i]["Time"]:.2f}s  0.2.5 | 0.2.6',fill='white')
        for k,label in enumerate(['old','candidate']):
            image=Image.open(p/f'motion/{actor}-{i:03d}-{label}.png').resize((160,160))
            sheet.paste(image,(x+k*160,y+22))
    sheet.save(dest/f'{actor}-full.png')
    # Every rendered mouth pose, paired old/candidate, without temporal gaps.
    sheet=Image.new('RGB',(960,((len(frames)+3)//4)*115),(24,24,24));draw=ImageDraw.Draw(sheet)
    for i,f in enumerate(frames):
        x=(i%4)*240;y=(i//4)*115
        draw.text((x+3,y+2),f'{f["Time"]:.2f}s old | new',fill='white')
        for k,label in enumerate(['old','candidate']):
            image=Image.open(p/f'motion/{actor}-{i:03d}-{label}.png').crop((85,180,255,310)).resize((120,92))
            sheet.paste(image,(x+k*120,y+20))
    sheet.save(dest/f'{actor}-mouths.png')
