"""Independent NVIDIA solved-skin point-cloud render; no invented topology."""
import bpy,json,numpy as np,struct
from pathlib import Path
from mathutils import Vector
root=Path(__file__).resolve().parents[2];p=root/'research/intro-a2f/correction-0.2.3'
dest=p/'source-preview';dest.mkdir(exist_ok=True)
src=np.load(root/'output/sdk-validation/_data/generated/audio2face-sdk/samples/data/multi-diffusion/bs_skin_Mark.npz')
names=json.loads((root/'output/intro-a2f/channels.json').read_text())[:-1]
skin=src['neutral'];basis=np.stack([src[n] for n in names])
clip=(root/'output/intro-a2f/clips/VO_65217_M.mefc').read_bytes()
c,count=struct.unpack_from('<II',clip,8)
data=np.frombuffer(clip[32:],dtype=np.dtype([('t','<u4'),('w','<f4',(c,))]))
w=data['w'][:,:37]
indices=[0,int(w[:,names.index('jawOpen')].argmax()),int(w[:,names.index('mouthClose')].argmax()),int(w[:,names.index('browInnerUp')].argmax())]
# Source scalar coordinates: +Y up, +Z forward. Align mouth centroid for the
# display only. Never transfer this surface onto the game mesh.
energy=np.linalg.norm(src['mouthFunnel'],axis=1);center=(skin*energy[:,None]).sum(0)/energy.sum()
def display(a):return np.stack([(a[:,2]-center[2])*.6+9,(a[:,0]-center[0])*.6,(a[:,1]-center[1])*.6+164.7],1)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
prefs=bpy.context.preferences.addons['cycles'].preferences;prefs.compute_device_type='OPTIX';prefs.get_devices()
gpus=[]
for d in prefs.devices:
    d.use=d.type=='OPTIX'
    if d.use:gpus.append(d.name)
assert gpus
s=bpy.context.scene;s.render.engine='CYCLES';s.cycles.device='GPU';s.cycles.samples=24
s.render.resolution_x=480;s.render.resolution_y=480;s.render.resolution_percentage=100;s.world.color=(.18,.18,.18)
mat=bpy.data.materials.new('Source points');mat.diffuse_color=(.45,.45,.45,1)
bm=bpy.data.meshes.new('NVIDIA solved skin');bm.from_pydata(display(skin).tolist(),[],[]);bm.update()
ob=bpy.data.objects.new('NVIDIA solved skin point cloud',bm);s.collection.objects.link(ob)
mod=ob.modifiers.new('Explicit point-cloud display','NODES');group=bpy.data.node_groups.new('Point spheres','GeometryNodeTree');mod.node_group=group
group.interface.new_socket(name='Geometry',in_out='INPUT',socket_type='NodeSocketGeometry')
group.interface.new_socket(name='Geometry',in_out='OUTPUT',socket_type='NodeSocketGeometry')
n=group.nodes;l=group.links;a=n.new('NodeGroupInput');out=n.new('NodeGroupOutput');points=n.new('GeometryNodeMeshToPoints');points.mode='VERTICES'
ico=n.new('GeometryNodeMeshIcoSphere');ico.inputs['Radius'].default_value=.045;ico.inputs['Subdivisions'].default_value=1
material=n.new('GeometryNodeSetMaterial');material.inputs['Material'].default_value=mat
instances=n.new('GeometryNodeInstanceOnPoints')
l.new(a.outputs['Geometry'],points.inputs['Mesh']);l.new(ico.outputs['Mesh'],material.inputs['Geometry'])
l.new(material.outputs['Geometry'],instances.inputs['Instance']);l.new(points.outputs['Points'],instances.inputs['Points']);l.new(instances.outputs['Instances'],out.inputs['Geometry'])
bpy.ops.object.camera_add(location=(33,-8,171));cam=bpy.context.object
cam.rotation_euler=(Vector((5,0,169))-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO';cam.data.ortho_scale=15;s.camera=cam
for loc,power in [((25,-12,185),1800),((20,15,177),1200)]:
    bpy.ops.object.light_add(type='AREA',location=loc);light=bpy.context.object;light.data.energy=power;light.data.size=12
    light.rotation_euler=(Vector((5,0,170))-light.location).to_track_quat('-Z','Y').to_euler()
frames=[]
for label,i in zip(['neutral','jaw-peak','close-peak','brow-peak'],indices):
    # This reconstructs the solved source basis, not the original neural skin
    # output or a claim of an exact NVIDIA demonstration reproduction.
    points=display(skin+np.einsum('c,cvi->vi',w[i],basis))
    bm.vertices.foreach_set('co',points.astype('f4').reshape(-1));bm.update()
    s.render.filepath=str(dest/f'{label}.png');bpy.ops.render.render(write_still=True)
    frames.append({'Label':label,'AudioTime':float(data['t'][i]/16000),'ClipFrame':i})
(dest/'render.json').write_text(json.dumps({'Source':'NVIDIA Mark solved blendshape basis; 37 played controls; unconnected source vertices shown as points','NotRawNetworkGeometry':True,'RetargetApplied':False,'Device':'GPU OptiX','GPUs':gpus,'Frames':frames,'InGameValidated':False},indent=2))
