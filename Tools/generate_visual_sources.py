"""Reproducible original textures and a rounded box, generated without external assets."""
from pathlib import Path
import numpy as np
from PIL import Image

root = Path(__file__).resolve().parent / 'Generated'
root.mkdir(exist_ok=True)
rng = np.random.default_rng(41)
n = 512
y, x = np.mgrid[:n, :n] / n
noise = rng.normal(0, 1, (n, n))
grain = np.sin((x * 29 + .06*np.sin(y*6)+.025*np.sin(y*17))*np.pi)
wood = np.clip(.80 + grain*.025 + np.sin(x*420+y*1.5)*.012 + noise*.006, .5, .95)
fabric = np.clip(.76 + .07*np.sin(x*n*np.pi/2)*np.sin(y*n*np.pi/2)+noise*.035, .4, .95)
plaster = np.clip(.84 + noise*.025, .5, 1)
for name, value, strength in [('Wood',wood,.7),('Fabric',fabric,.6),('Plaster',plaster,.18)]:
    Image.fromarray(np.uint8(np.repeat(value[:,:,None],3,axis=2)*255)).save(root/f'T_{name}.png')
    dx=(np.roll(value,-1,axis=1)-np.roll(value,1,axis=1))*strength
    dy=(np.roll(value,-1,axis=0)-np.roll(value,1,axis=0))*strength
    normal=np.stack([-dx,-dy,np.ones_like(dx)],axis=-1)
    normal/=np.linalg.norm(normal,axis=-1,keepdims=True)
    Image.fromarray(np.uint8((normal*.5+.5)*255)).save(root/f'T_{name}_N.png')

vertices=[]; normals=[]; uvs=[]; triangles=[]
for axis in range(3):
    for sign in (-1,1):
        start=len(vertices)
        for j in range(17):
            for i in range(17):
                p=np.zeros(3); p[axis]=sign*.5
                p[(axis+1)%3]=i/16-.5; p[(axis+2)%3]=j/16-.5
                core=np.clip(p,-.43,.43); normal=p-core
                normal/=np.linalg.norm(normal)
                vertices.append((core+normal*.07)*100)
                normals.append(normal); uvs.append((i/16,j/16))
        for j in range(16):
            for i in range(16):
                a=start+j*17+i; b=a+1; c=a+17; d=c+1
                triangles.extend([(a,b,d),(a,d,c)] if sign>0 else [(a,d,b),(a,c,d)])
with (root/'SM_RoundedBox.obj').open('w') as f:
    f.write('o RoundedBox\n')
    for p in vertices: f.write('v %.6f %.6f %.6f\n'%tuple(p))
    for p in uvs: f.write('vt %.6f %.6f\n'%p)
    for p in normals: f.write('vn %.6f %.6f %.6f\n'%tuple(p))
    for tri in triangles: f.write('f '+' '.join(f'{i+1}/{i+1}/{i+1}' for i in tri)+'\n')
print(root)
