"""Conservative full-turn AABB search; no mesh scaling or engine dependencies."""
import json, math, itertools
from pathlib import Path
root=Path(__file__).resolve().parents[1]
rows=json.loads((root/'Documentation/BedroomLiveInspection.json').read_text(encoding='utf-8'))
pivot=[815,1070,350]
camera=[815,1070,100]
room_min=[160,395,-75]; room_max=[1470,1740,780]
specs={3:([1,2,4],[1,3,2]),4:([0,4],[2,1]),5:([2,3,4],[2,1,3]),8:([0,3,4],[3,2,1])}
axes=[(1,1),(0,-1),(0,1),(1,-1),(2,1)]
def rotate(p,axis,angle):
    a,b=[i for i in range(3) if i!=axis]
    if axis==1:a,b=b,a
    q=p[:];c=math.cos(angle);s=math.sin(angle)
    q[a]=p[a]*c-p[b]*s;q[b]=p[a]*s+p[b]*c
    return q
result={}
for item,(faces,goals) in specs.items():
    parts=[r for r in rows if 'RH_ITEM_'+str(item) in r['tags']]
    if len(parts)>len(faces):
        faces=[int(next(t for t in p['tags'] if t.startswith('RH_FACE_'))[8:]) for p in parts]
        goals=[int(next(t for t in p['tags'] if t.startswith('RH_GOAL_'))[8:]) for p in parts]
    oldcenter=[(min(p['center'][i]-p['extent'][i] for p in parts)+max(p['center'][i]+p['extent'][i] for p in parts))/2 for i in range(3)]
    best=None
    for target in itertools.product(range(600,1101,100),range(700,1501,100),range(250,601,50)):
        clearance=1e9;outside=0
        for part,face in zip(parts,faces):
            offset=[part['center'][i]-oldcenter[i]+target[i]-pivot[i] for i in range(3)]
            axis,sign=axes[face]
            for deg in range(0,360,15):
                angle=math.radians(deg);c=rotate(offset,axis,angle)
                ext=[abs(v) for v in rotate(part['extent'],axis,angle)]
                # Rotate AABB half axes, then sum absolute contributions.
                ext=[sum(abs(rotate([part['extent'][j] if k==j else 0 for k in range(3)],axis,angle)[i]) for j in range(3)) for i in range(3)]
                lo=[c[i]+pivot[i]-ext[i] for i in range(3)];hi=[c[i]+pivot[i]+ext[i] for i in range(3)]
                outside=max(outside,max(max(room_min[i]-lo[i],hi[i]-room_max[i],0) for i in range(3)))
                distance=math.sqrt(sum(max(lo[i]-camera[i],camera[i]-hi[i],0)**2 for i in range(3)))
                clearance=min(clearance,distance)
        # Prefer safe views and spread the assembly locations by object.
        preferred={3:(800,1100,550),4:(650,750,350),5:(1100,1250,450),8:(800,1400,500)}[item]
        score=min(clearance,130)*5-outside*30-math.dist(target,preferred)*.15
        if best is None or score>best[0]:best=(score,target,clearance,outside)
    result[item]=dict(target=best[1],clearance=best[2],outside=best[3],faces=faces,goals=goals)
print(json.dumps(result,indent=2))
(root/'Documentation/BedroomLayoutPlan.json').write_text(json.dumps(dict(pivot=pivot,camera=camera,items=result),indent=2))
