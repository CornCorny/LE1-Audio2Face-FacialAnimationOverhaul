"""OptiX-only authoring preview; no game launch or CPU render fallback."""
import bpy,json,numpy as np
from pathlib import Path
from mathutils import Vector
root=Path(__file__).resolve().parents[2];study=root/'research/intro-a2f/correction-0.2.8';dest=study/'preview';dest.mkdir(exist_ok=True)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
prefs=bpy.context.preferences.addons['cycles'].preferences;prefs.compute_device_type='OPTIX';prefs.get_devices();gpus=[]
for d in prefs.devices:
    d.use=d.type=='OPTIX'
    if d.use:gpus.append(d.name)
assert gpus,'Refuse CPU render fallback'
s=bpy.context.scene;s.render.engine='CYCLES';s.cycles.device='GPU';s.cycles.samples=16
s.render.resolution_x=320;s.render.resolution_y=320;s.render.resolution_percentage=100;s.world.color=(.18,.18,.18)
mat=bpy.data.materials.new('Clay');mat.diffuse_color=(.45,.45,.45,1)
bpy.ops.object.camera_add(location=(33,-8,171));cam=bpy.context.object
cam.rotation_euler=(Vector((5,0,169))-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO';cam.data.ortho_scale=15;s.camera=cam
for loc,power,size in [((25,-12,185),1800,12),((20,15,177),1200,10),((-5,4,182),1000,8)]:
    bpy.ops.object.light_add(type='AREA',location=loc);light=bpy.context.object;light.data.energy=power;light.data.shape='DISK';light.data.size=size
    light.rotation_euler=(Vector((5,0,170))-light.location).to_track_quat('-Z','Y').to_euler()
data=np.load(study/'preview.npz');bm=bpy.data.meshes.new('Compatible head');bm.from_pydata(data['neutral'].tolist(),[],data['indices'].tolist());bm.update()
ob=bpy.data.objects.new('Compatible head',bm);s.collection.objects.link(ob);bm.materials.append(mat)
for poly in bm.polygons:poly.use_smooth=True
meta=json.loads((study/'preview-inputs.json').read_text());files=[]
for i,record in enumerate(meta['Frames']):
    bm.vertices.foreach_set('co',data['candidate'][i].reshape(-1));bm.update();name=f'{i:02d}.png';s.render.filepath=str(dest/name);bpy.ops.render.render(write_still=True)
    files.append(dict(record,File=name))
(dest/'render.json').write_text(json.dumps({'Engine':'Cycles OptiX','GPUs':gpus,'Frames':files,'GameLaunched':False,'RuntimeValidated':False,'Limitation':'Clay omits game eyes/materials and vanilla contribution'},indent=2))
