"""Background Cycles OptiX render of original/candidate at identical audio times."""
import bpy,json,numpy as np,sys,argparse
from pathlib import Path
from mathutils import Vector
parser=argparse.ArgumentParser();parser.add_argument('--revision',default='0.2.3')
custom_args=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else sys.argv[1:]
args,_=parser.parse_known_args(custom_args)
root=Path(__file__).resolve().parents[2];p=root/f'research/intro-a2f/correction-{args.revision}'
motion='--motion' in sys.argv
dest=p/('motion' if motion else 'previews');dest.mkdir(exist_ok=True)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
prefs=bpy.context.preferences.addons['cycles'].preferences;prefs.compute_device_type='OPTIX';prefs.get_devices()
gpus=[]
for d in prefs.devices:
    d.use=d.type=='OPTIX'
    if d.use:gpus.append(d.name)
assert gpus,'No GPU; refuse CPU fallback'
s=bpy.context.scene;s.render.engine='CYCLES';s.cycles.device='GPU';s.cycles.samples=16 if motion else 32
s.render.resolution_x=320 if motion else 480;s.render.resolution_y=s.render.resolution_x;s.render.resolution_percentage=100
s.world.color=(.18,.18,.18)
mat=bpy.data.materials.new('clay');mat.diffuse_color=(.45,.45,.45,1)
bpy.ops.object.camera_add(location=(33,-8,171));cam=bpy.context.object
cam.rotation_euler=(Vector((5,0,169))-cam.location).to_track_quat('-Z','Y').to_euler()
cam.data.type='ORTHO';cam.data.ortho_scale=15;s.camera=cam
for loc,power,size in [((25,-12,185),1800,12),((20,15,177),1200,10),((-5,4,182),1000,8)]:
    bpy.ops.object.light_add(type='AREA',location=loc);l=bpy.context.object;l.data.energy=power;l.data.shape='DISK';l.data.size=size
    l.rotation_euler=(Vector((5,0,170))-l.location).to_track_quat('-Z','Y').to_euler()
meta=json.loads((p/('motion-inputs.json' if motion else 'preview-inputs.json')).read_text());files=[]
for record in meta['Frames']:
    actor=record['Actor'];g=json.loads((root/f"research/intro-a2f/ownership/{record['HeadMesh']}-facegraph.json").read_text(encoding='utf-8-sig'))
    data=np.load(p/f'{actor}-{"motion" if motion else "preview"}.npz');bm=bpy.data.meshes.new(actor)
    bm.from_pydata(data['old'][0].tolist(),[],np.array(g['LODs'][0]['Indices']).reshape(-1,3).tolist());bm.update()
    ob=bpy.data.objects.new(actor,bm);s.collection.objects.link(ob);bm.materials.append(mat)
    for poly in bm.polygons:poly.use_smooth=True
    for i,t in enumerate(data['times']):
        for label in ['old','candidate']:
            bm.vertices.foreach_set('co',data[label][i].reshape(-1));bm.update()
            f=dest/f'{actor}-{i:03d}-{label}.png' if motion else dest/f'{actor}-{i}-{label}.png'
            s.render.filepath=str(f);bpy.ops.render.render(write_still=True)
            files.append({'Actor':actor,'Time':float(t),'Version':label,'File':f.name})
    bpy.data.objects.remove(ob,do_unlink=True)
(dest/'render.json').write_text(json.dumps({'Engine':'Cycles','Device':'GPU OptiX','GPUs':gpus,'Samples':s.cycles.samples,'PreviewFPS':12 if motion else None,'Frames':files,'GameLaunched':False,'InGameValidated':False},indent=2))
