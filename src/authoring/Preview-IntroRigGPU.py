"""Blender background: actual Cycles OptiX renders, no AI image generation."""
import bpy,json,numpy as np,struct
from pathlib import Path
from mathutils import Vector
root=Path(__file__).resolve().parents[2];out=root/'output/intro-a2f'
preview=root/'research/intro-a2f/correction-0.2.1/previews';preview.mkdir(parents=True,exist_ok=True)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
prefs=bpy.context.preferences.addons['cycles'].preferences;prefs.compute_device_type='OPTIX';prefs.get_devices()
devices=[]
for d in prefs.devices:
    d.use=d.type=='OPTIX'
    if d.use:devices.append(d.name)
assert devices,'OptiX GPU unavailable; refuse CPU fallback'
scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.device='GPU';scene.cycles.samples=40
scene.render.resolution_x=640;scene.render.resolution_y=640;scene.render.resolution_percentage=100
scene.world.color=(.18,.18,.18)
mat=bpy.data.materials.new('Neutral clay');mat.diffuse_color=(.42,.42,.42,1)
bpy.ops.object.camera_add(location=(33,-11,171));cam=bpy.context.object
cam.rotation_euler=(Vector((5,0,169))-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO';cam.data.ortho_scale=16;scene.camera=cam
for location,power,size in [((25,-12,185),1800,12),((20,15,177),1200,10),((-5,4,182),1000,8)]:
    bpy.ops.object.light_add(type='AREA',location=location);light=bpy.context.object;light.data.energy=power;light.data.shape='DISK';light.data.size=size
    light.rotation_euler=(Vector((5,0,170))-light.location).to_track_quat('-Z','Y').to_euler()
names=json.loads((out/'channels.json').read_text());manifest=json.loads((out/'scene-manifest.json').read_text());report=[]
for actor in ['Shepard','Joker','Kaidan']:
    data=json.loads((out/f'meshes/{actor}.json').read_text(encoding='utf-8-sig'));g=json.loads((root/f'research/intro-a2f/ownership/{actor}-facegraph.json').read_text(encoding='utf-8-sig'))
    pos=np.array([v['Position'] for v in data['LODs'][0]['Vertices']]);indices=g['LODs'][0]['Indices']
    basis=np.load(out/f'meshes/{actor}-lod0-deltas.npy');jaw=basis[names.index('jawOpen')]*.6
    best=None
    for line in manifest['Lines']:
        if line['Actor']!=actor:continue
        b=(out/'clips'/line['Clip']).read_bytes();channels,count=struct.unpack_from('<II',b,8)
        w=np.frombuffer(b[32:],dtype=np.dtype([('t','<u4'),('w','<f4',(channels,))]))['w']
        score=w[:,names.index('jawOpen')]+w[:,names.index('mouthFunnel')]*.3
        frame=int(np.argmax(score))
        if best is None or score[frame]>best[0]:best=(float(score[frame]),line['StringRef'],frame,w[frame].copy())
    speech=np.einsum('c,cvi->vi',best[3],basis)
    bm=bpy.data.meshes.new(actor);bm.from_pydata(pos.tolist(),[],np.array(indices).reshape(-1,3).tolist());bm.update()
    obj=bpy.data.objects.new(actor,bm);scene.collection.objects.link(obj);obj.data.materials.append(mat)
    for poly in bm.polygons:poly.use_smooth=True
    for name,delta in [('neutral',np.zeros_like(pos)),('jaw60',jaw),('speech',speech)]:
        for v,p in zip(bm.vertices,pos+delta):v.co=p
        bm.update();scene.render.filepath=str(preview/f'{actor}-{name}.png');bpy.ops.render.render(write_still=True)
    report.append({'Actor':actor,'SpeechStringRef':best[1],'SpeechFrame':best[2]})
    bpy.data.objects.remove(obj,do_unlink=True)
(preview/'render.json').write_text(json.dumps({'Engine':'Cycles','Device':'GPU OptiX','GPUs':devices,'Samples':40,'Frames':report,'InGameValidation':False},indent=2))
