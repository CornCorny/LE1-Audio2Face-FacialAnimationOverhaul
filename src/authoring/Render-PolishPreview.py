"""Render actual candidate deformations on original meshes using OptiX only."""
import bpy,json,numpy as np,sys
from pathlib import Path
from mathutils import Vector
root=Path(__file__).resolve().parents[2];p=root/'research/intro-a2f/correction-0.2.7'
final='--final' in sys.argv;dest=p/('motion' if final else 'variants');dest.mkdir(exist_ok=True)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
prefs=bpy.context.preferences.addons['cycles'].preferences;prefs.compute_device_type='OPTIX';prefs.get_devices();gpus=[]
for d in prefs.devices:
    d.use=d.type=='OPTIX'
    if d.use:gpus.append(d.name)
assert gpus,'Refuse CPU rendering fallback'
s=bpy.context.scene;s.render.engine='CYCLES';s.cycles.device='GPU';s.cycles.samples=16 if final else 24
s.render.resolution_x=320 if final else 480;s.render.resolution_y=s.render.resolution_x;s.render.resolution_percentage=100;s.world.color=(.18,.18,.18)
mat=bpy.data.materials.new('Clay preview');mat.diffuse_color=(.45,.45,.45,1)
bpy.ops.object.camera_add(location=(33,-8,171));cam=bpy.context.object;cam.rotation_euler=(Vector((5,0,169))-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO';cam.data.ortho_scale=15;s.camera=cam
for loc,power,size in [((25,-12,185),1800,12),((20,15,177),1200,10),((-5,4,182),1000,8)]:
    bpy.ops.object.light_add(type='AREA',location=loc);l=bpy.context.object;l.data.energy=power;l.data.shape='DISK';l.data.size=size;l.rotation_euler=(Vector((5,0,170))-l.location).to_track_quat('-Z','Y').to_euler()
meta=json.loads((p/('final-motion-inputs.json' if final else 'variant-inputs.json')).read_text());files=[]
for rec in meta['Frames']:
    actor=rec['Actor'];graph=json.loads((root/f"research/intro-a2f/ownership/{rec['HeadMesh']}-facegraph.json").read_text(encoding='utf-8-sig'))
    data=np.load(p/f'{actor}-{"final-motion" if final else "variants"}.npz');bm=bpy.data.meshes.new(actor)
    bm.from_pydata(data[rec['Labels'][0]][0].tolist(),[],np.array(graph['LODs'][0]['Indices']).reshape(-1,3).tolist());bm.update()
    ob=bpy.data.objects.new(actor,bm);s.collection.objects.link(ob);bm.materials.append(mat)
    for poly in bm.polygons:poly.use_smooth=True
    for i,tm in enumerate(data['times']):
        for label in rec['Labels']:
            bm.vertices.foreach_set('co',data[label][i].reshape(-1));bm.update()
            name=f'{actor}-{i:03d}-{label}.png';s.render.filepath=str(dest/name);bpy.ops.render.render(write_still=True)
            files.append({'Actor':actor,'Time':float(tm),'Version':label,'File':name})
    bpy.data.objects.remove(ob,do_unlink=True)
(dest/'render.json').write_text(json.dumps({'Engine':'Cycles','Device':'GPU OptiX','GPUs':gpus,'Frames':files,'GameLaunched':False,'InGameValidated':False,'Limitation':'Clay preview omits vanilla facial contribution, eyes and game materials'},indent=2))
