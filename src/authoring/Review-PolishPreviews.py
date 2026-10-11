"""Make inspection sheets from actual local OptiX preview frames."""
import json,sys
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
root=Path(__file__).resolve().parents[2];p=root/'research/intro-a2f/correction-0.2.7'
font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',22)
if '--motion' in sys.argv:
    meta=json.loads((p/'motion/render.json').read_text())
    for actor in ['Kaidan','Joker','Shepard']:
        rows=[x for x in meta['Frames'] if x['Actor']==actor and x['Version']=='old']
        for page in range((len(rows)+15)//16):
            sheet=Image.new('RGB',(1280,1488),(18,18,18));d=ImageDraw.Draw(sheet)
            for k,r in enumerate(rows[page*16:(page+1)*16]):
                x=k%4*320;y=k//4*372
                for label,xx in [('old',x),('candidate',x+160)]:
                    name=r['File'].replace('-old.png',f'-{label}.png')
                    # Keep aspect ratio for mouth comparison: 200x110 ->160x88.
                    im=Image.open(p/'motion'/name).crop((70,190,270,300)).resize((160,88))
                    sheet.paste(im,(xx,y+28))
                    full=Image.open(p/'motion'/name).resize((160,160));sheet.paste(full,(xx,y+124))
                d.text((x+4,y+2),f"{actor} {r['Time']:.2f}s",font=font,fill='white')
            sheet.save(p/f'{actor}-motion-sheet-{page+1}.jpg',quality=94)
    sys.exit()
meta=json.loads((p/'variant-inputs.json').read_text())
for rec in meta['Frames']:
    actor=rec['Actor']
    for page in range(2):
        sheet=Image.new('RGB',(1920,1524),(18,18,18));d=ImageDraw.Draw(sheet)
        for row,i in enumerate(range(page*3,page*3+3)):
            for col,label in enumerate(rec['Labels']):
                x=col*480;y=row*508
                sheet.paste(Image.open(p/f'variants/{actor}-{i:03d}-{label}.png'),(x,y+28))
                d.text((x+4,y+2),f'{actor} {label} {rec["Times"][i]:.2f}s',font=font,fill='white')
        sheet.save(p/f'{actor}-variants-{page+1}.jpg',quality=94)
