"""Generate original engine test fixtures. No downloaded game content."""
import json, struct, math, zlib, binascii, wave
from pathlib import Path
OUT=Path(__file__).resolve().parents[1]/'assets'
OUT.mkdir(exist_ok=True)
def png():
    def chunk(t,d): return struct.pack('>I',len(d))+t+d+struct.pack('>I',binascii.crc32(t+d)&0xffffffff)
    raw=b''.join(b'\0'+b''.join(bytes((210,220,230,255) if ((x//8+y//8)%2) else (85,110,135,255)) for x in range(32)) for y in range(32))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>2I5B',32,32,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')
class GLB:
    def __init__(self):
        self.data=bytearray(); self.j={'asset':{'version':'2.0','generator':'The One original fixture generator'},'buffers':[{}],'bufferViews':[],'accessors':[], 'materials':[], 'meshes':[], 'nodes':[], 'scenes':[{'nodes':[]}], 'scene':0}
    def blob(self,data):
        while len(self.data)%4:self.data.append(0)
        i=len(self.j['bufferViews']);self.j['bufferViews'].append({'buffer':0,'byteOffset':len(self.data),'byteLength':len(data)});self.data.extend(data);return i
    def accessor(self,values,kind,component=5126):
        dims={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}[kind];flat=[v for row in values for v in (row if isinstance(row,(list,tuple)) else [row])]
        view=self.blob(struct.pack('<'+('f' if component==5126 else 'H')*len(flat),*flat));i=len(self.j['accessors'])
        a={'bufferView':view,'componentType':component,'count':len(values),'type':kind}
        if kind=='VEC3':a.update(min=[min(row[k] for row in values) for k in range(3)],max=[max(row[k] for row in values) for k in range(3)])
        self.j['accessors'].append(a);return i
    def material(self,name,color,texture=False,metallic=0,roughness=0.6):
        m={'name':name,'pbrMetallicRoughness':{'baseColorFactor':color,'metallicFactor':metallic,'roughnessFactor':roughness}}
        if texture:
            if 'images' not in self.j:
                self.j['images']=[{'name':'Original checker','bufferView':self.blob(png()),'mimeType':'image/png'}];self.j['textures']=[{'source':0}]
            m['pbrMetallicRoughness']['baseColorTexture']={'index':0}
        if color[3]<1:m['alphaMode']='BLEND'
        self.j['materials'].append(m);return len(self.j['materials'])-1
    def mesh(self,name,verts,material,skinned=False):
        attrs={'POSITION':self.accessor([v[0] for v in verts],'VEC3'),'NORMAL':self.accessor([v[1] for v in verts],'VEC3'),'TEXCOORD_0':self.accessor([v[2] for v in verts],'VEC2')}
        if skinned:
            attrs['JOINTS_0']=self.accessor([(v[3],0,0,0) for v in verts],'VEC4',5123)
            attrs['WEIGHTS_0']=self.accessor([(1,0,0,0)]*len(verts),'VEC4')
        self.j['meshes'].append({'name':name,'primitives':[{'attributes':attrs,'material':material,'mode':4}]});return len(self.j['meshes'])-1
    def node(self,name,mesh=None,**kw):
        n={'name':name,**kw}
        if mesh is not None:n['mesh']=mesh
        self.j['nodes'].append(n);return len(self.j['nodes'])-1
    def save(self,name):
        self.j['buffers'][0]['byteLength']=len(self.data);j=json.dumps(self.j,separators=(',',':')).encode();j+=b' '*((-len(j))%4);b=bytes(self.data);b+=b'\0'*((-len(b))%4)
        content=struct.pack('<I4s',len(j),b'JSON')+j+struct.pack('<I4s',len(b),b'BIN\0')+b
        (OUT/name).write_bytes(struct.pack('<4sII',b'glTF',2,len(content)+12)+content)
def quad(points,normal,joint=0,uvscale=1):
    uv=[(0,0),(uvscale,0),(uvscale,uvscale),(0,uvscale)]
    return [(points[i],normal,uv[i],joint) for i in (0,1,2,0,2,3)]
def box(p,s,joint=0):
    x,y,z=p;X,Y,Z=[a+b for a,b in zip(p,s)];v=[]
    for pts,n in [([(x,y,z),(x,Y,z),(X,Y,z),(X,y,z)],(0,0,-1)), ([(X,y,Z),(X,Y,Z),(x,Y,Z),(x,y,Z)],(0,0,1)),
        ([(x,y,Z),(x,Y,Z),(x,Y,z),(x,y,z)],(-1,0,0)), ([(X,y,z),(X,Y,z),(X,Y,Z),(X,y,Z)],(1,0,0)),
        ([(x,Y,z),(x,Y,Z),(X,Y,Z),(X,Y,z)],(0,1,0)), ([(x,y,Z),(x,y,z),(X,y,z),(X,y,Z)],(0,-1,0))]:v+=quad(pts,n,joint)
    return v
level=GLB();floor=level.material('Textured slate',[0.6,0.7,0.8,1],True);stone=level.material('Sandstone',[0.66,0.48,0.24,1]);blue=level.material('Blue ceramic',[0.06,0.5,0.65,1],False,0.3,0.24)
v=quad([(-18,0,-18),(-18,0,18),(18,0,18),(18,0,-18)],(0,1,0),uvscale=24)
root=level.node('Level root');ground=level.node('Ground',level.mesh('Ground mesh',v,floor));children=[ground]
for i in range(12):
    a=i*math.tau/12;x,z=math.sin(a)*8,math.cos(a)*8
    children.append(level.node(f'Pillar {i}',level.mesh(f'Pillar {i}',box((x-.55,0,z-.55),(1.1,1.2+(i%4)*.65,1.1)),stone)))
steps=[]
for i in range(5):steps+=box((-5,0,-4-i),(3,.25*(i+1),.85))
children.append(level.node('Stair test',level.mesh('Steps',steps,blue)))
arch=box((-2,0,-9),(1,4,1))+box((2,0,-9),(1,4,1))+box((-2,4,-9),(5,1,1))
children.append(level.node('Archway',level.mesh('Arch',arch,stone)));level.j['nodes'][root]['children']=children;level.j['scenes'][0]['nodes']=[root];level.save('level.glb')
crate=GLB();mat=crate.material('Crate',[0.9,.48,.13,1],True);n=crate.node('Crate mesh',crate.mesh('Box',box((-.5,-.5,-.5),(1,1,1)),mat));crate.j['scenes'][0]['nodes']=[n];crate.save('crate.glb')
robot=GLB();mat=robot.material('Robot ceramic',[.1,.8,.7,1],True,.4,.3)
v=box((-.4,0,-.25),(.3,.8,.5),0)+box((.1,0,-.25),(.3,.8,.5),0)+box((-.45,.8,-.25),(.9,.9,.5),1)+box((-.3,1.7,-.3),(.6,.55,.6),1)+box((-.75,.85,-.2),(.25,.8,.4),1)+box((.5,.85,-.2),(.25,.8,.4),1)
mesh=robot.mesh('Weighted robot',v,mat,True);node=robot.node('Robot skinned mesh',mesh,skin=0);joint0=robot.node('Hips');joint1=robot.node('Spine',translation=[0,.8,0]);robot.j['nodes'][joint0]['children']=[joint1]
identity=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1];inv=identity.copy();inv[13]=-.8
robot.j['skins']=[{'name':'Robot rig','joints':[joint0,joint1],'inverseBindMatrices':robot.accessor([identity,inv],'MAT4'),'skeleton':joint0}]
times=[0,.5,1,1.5,2];angles=[0,.4,0,-.4,0];rot=[(0,0,math.sin(a/2),math.cos(a/2)) for a in angles]
robot.j['animations']=[{'name':'Wave','samplers':[{'input':robot.accessor(times,'SCALAR'),'output':robot.accessor(rot,'VEC4'),'interpolation':'LINEAR'}],'channels':[{'sampler':0,'target':{'node':joint1,'path':'rotation'}}]}]
robot.j['scenes'][0]['nodes']=[node,joint0];robot.save('robot.glb')
entities=[{'id':1,'name':'Imported Level','asset':'level.glb','body':'static'}, {'id':2,'name':'Animated Robot','asset':'robot.glb','position':[-2,0,0]},
    {'id':3,'name':'Rotating Exhibit','position':[3,1,0],'behavior':'spin','speed':.6}, {'id':4,'name':'Exhibit Child','asset':'crate.glb','parent':3,'position':[0,1,0],'scale':[.8,.8,.8]}]
for i in range(5):entities.append({'id':10+i,'name':f'Physics Crate {i+1}','asset':'crate.glb','position':[1+(i%2)*1.1,2+i*1.2,-3],'body':'dynamic','mass':2})
for i,pos in enumerate([[-3,.8,4],[3,.8,4],[0,.8,-6]]):entities.append({'id':30+i,'name':f'Collectible {i+1}','asset':'crate.glb','position':pos,'scale':[.4,.4,.4],'behavior':'spin','speed':1.4})
(OUT/'sandbox.one').write_text(json.dumps({'version':2,'entities':entities},indent=2))
with wave.open(str(OUT/'interact.wav'),'wb') as w:
    w.setnchannels(1);w.setsampwidth(2);w.setframerate(44100)
    w.writeframes(b''.join(struct.pack('<h',int(math.sin(i*math.tau*(660+220*i/11025)/44100)*9000*(1-i/11025)**2)) for i in range(11025)))
print('Generated level, skinned animated robot, crate, scene and audio fixtures')

conv=OUT/'conversion';conv.mkdir(exist_ok=True);(conv/'checker.png').write_bytes(png())
(conv/'cube.mtl').write_text('newmtl checker\nKd 1 1 1\nmap_Kd checker.png\n')
lines=['mtllib cube.mtl','usemtl checker']
verts=box((-.5,-.5,-.5),(1,1,1))
for p,n,uv,j in verts:lines.append('v '+' '.join(map(str,p)))
for p,n,uv,j in verts:lines.append('vt '+' '.join(map(str,uv)))
for p,n,uv,j in verts:lines.append('vn '+' '.join(map(str,n)))
for i in range(0,len(verts),3):lines.append('f '+' '.join(f'{k}/{k}/{k}' for k in range(i+1,i+4)))
(conv/'cube.obj').write_text('\n'.join(lines)+'\n')
