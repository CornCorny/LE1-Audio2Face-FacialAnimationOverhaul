"""Package local GPU previews with authoring audio; never include in player mod."""
import json,subprocess,argparse
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('--revision',default='0.2.3');parser.add_argument('--previous',default='0.2.2');args=parser.parse_args()
root=Path(__file__).resolve().parents[2];p=root/f'research/intro-a2f/correction-{args.revision}'
meta=json.loads((p/'motion/render.json').read_text())
assert meta['Device']=='GPU OptiX' and len(meta['Frames'])>200
assert all((p/'motion'/x['File']).is_file() for x in meta['Frames'])
ff=root/'tools/ffmpeg/ffmpeg-9.0.2-essentials_build/bin/ffmpeg.exe'
probe=ff.with_name('ffprobe.exe');report=[]
font="fontfile='C\\:/Windows/Fonts/arial.ttf'"
for actor,id in [('Shepard',65217),('Joker',102170),('Kaidan',102169)]:
    dest=p/f'{actor}-AB-{args.previous}-vs-{args.revision}.mp4'
    graph=f"[0:v][1:v]hstack=inputs=2,drawtext={font}:text='{args.previous}':x=10:y=10:fontcolor=white:box=1:boxcolor=black@0.5,drawtext={font}:text='{args.revision}':x=330:y=10:fontcolor=white:box=1:boxcolor=black@0.5[v]"
    cmd=[str(ff),'-hide_banner','-loglevel','error','-y','-framerate','12','-i',str(p/f'motion/{actor}-%03d-old.png'),'-framerate','12','-i',str(p/f'motion/{actor}-%03d-candidate.png'),'-i',str(root/f'output/intro-a2f/audio/VO_{id}_M.wav'),'-filter_complex',graph,'-map','[v]','-map','2:a:0','-c:v','h264_nvenc','-gpu','0','-preset','p4','-cq','18','-pix_fmt','yuv420p','-c:a','aac','-shortest',str(dest)]
    subprocess.run(cmd,check=True)
    info=json.loads(subprocess.check_output([str(probe),'-v','error','-show_streams','-show_format','-of','json',str(dest)]))
    v=next(x for x in info['streams'] if x['codec_type']=='video')
    assert (v['width'],v['height'],v['codec_name'])==(640,320,'h264')
    report.append({'Actor':actor,'File':dest.name,'VideoEncoder':'h264_nvenc GPU0','Duration':float(info['format']['duration']),'PreviewOnly':True})
(p/'preview-videos.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
