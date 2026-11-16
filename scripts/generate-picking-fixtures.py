# Deterministic picking contract fixtures. Run from the repository root.
# Explicit geometric cases plus an independent world-plane oracle; no picking implementation is imported.
import json,math,struct
from pathlib import Path
f=lambda x:struct.unpack('f',struct.pack('f',x))[0]
I=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]
def matrix(x=1,y=1,z=1,t=(0,0,0)):
 a=I.copy();a[0]=x;a[5]=y;a[10]=z;a[12:15]=t;return a
def instance(id='front',world=None):return dict(id=id,assetId='box',world=world or I.copy())
def hit(id,p,n=(0,0,1),distance=4.5):return dict(id=id,position=p,normal=list(n),distance=distance)
cases=[]
def add(name,instances=None,origin=None,direction=None,expected=None,include=None,exclude=None,source=False,invalid=False):
 ray=dict(origin=origin or [.2,.07,5],direction=direction or [0,0,-2])
 if include is not None:ray['includeIds']=include
 if exclude is not None:ray['excludeIds']=exclude
 cases.append(dict(name=name,sourceSpace=source,instances=instances if instances is not None else [instance()],ray=ray,expected=expected,invalid=invalid))
front=hit('front',[.2,.07,.5]);pair=[instance('back',matrix(t=(0,0,-3))),instance()]
add('nearest-not-input-order',pair,expected=front)
add('include-back',pair,include=['back'],expected=hit('back',[.2,.07,-2.5],distance=7.5))
add('exclude-wins',pair,include=['front','back'],exclude=['front'],expected=hit('back',[.2,.07,-2.5],distance=7.5))
add('include-and-exclude-same',pair,include=['front'],exclude=['front'])
add('empty-include-is-all',pair,include=[],expected=front)
add('unknown-include',pair,include=['unknown'])
add('unknown-exclude',pair,exclude=['unknown'],expected=front)
add('filter-duplicates',pair,include=['back','back'],expected=hit('back',[.2,.07,-2.5],distance=7.5))
add('tie-first-input',[instance('z-first'),instance('a-second')],expected=hit('z-first',[.2,.07,.5]))
add('nonuniform',[instance(world=matrix(2,.5,3))],expected=hit('front',[.2,.07,1.5],distance=3.5))
add('mirrored-winding',[instance(world=matrix(-2,.5,3))],expected=hit('front',[.2,.07,1.5],n=(0,0,-1),distance=3.5))
shear=I.copy();shear[2]=.5
add('sheared-normal',[instance(world=shear)],expected=hit('front',[.2,.07,.6],n=(-1/math.sqrt(5),0,2/math.sqrt(5)),distance=4.4))
rot=[0,0,-1,0,0,1,0,0,1,0,0,0,0,0,0,1]
add('rotated-world-ray',[instance(world=rot)],origin=[5,.07,.2],direction=[-2,0,0],expected=hit('front',[.5,.07,.2],n=(1,0,0)))
add('tiny-invertible',[instance(world=matrix(1e-5,1e-5,1e-5))],origin=[2e-6,7e-7,1e-4],direction=[0,0,-1e-20],expected=hit('front',[2e-6,7e-7,5e-6],distance=9.5e-5))
add('large-invertible',[instance(world=matrix(1e12,1e12,1e12))],origin=[2e11,7e10,5e12],expected=hit('front',[2e11,7e10,5e11],distance=4.5e12))
add('original-model-space',origin=[2,1,5],source=True,expected=hit('front',[2,1,1],distance=4))
add('normalized-space-miss',origin=[2,1,5])
add('original-mirrored',[instance(world=matrix(-2,.5,3))],origin=[-4,.5,10],source=True,expected=hit('front',[-4,.5,3],n=(0,0,-1),distance=7))
add('inside-not-face-forward',origin=[0,0,0],direction=[0,0,1],expected=hit('front',[0,0,.5],distance=.5))
add('on-surface-zero-distance',origin=[.2,.07,.5],expected=hit('front',[.2,.07,.5],distance=0))
add('away-from-model',origin=[0,0,-3])
add('parallel-outside',origin=[0,2,0],direction=[1,0,0])
add('singular-instance',[instance(world=matrix(0,1,1))])
add('empty-scene',[])
add('large-direction',direction=[0,0,-1e30],expected=front)
add('zero-direction',direction=[0,0,0],invalid=True)
add('invalid-filter-id',include=[''],invalid=True)
def mul(a,b):return [f(sum(a[k*4+r]*b[c*4+k] for k in range(4))) for c in range(4) for r in range(4)]
def cross(a,b):return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
def dot(a,b):return sum(x*y for x,y in zip(a,b))
# Independent world-plane oracle for transformed box front faces. No inverse-ray or BVH code.
for i in range(48):
 t=(i+1)*.131;p=(i+1)*.177
 rz=[math.cos(t),math.sin(t),0,0,-math.sin(t),math.cos(t),0,0,0,0,1,0,0,0,0,1]
 ry=[math.cos(p),0,-math.sin(p),0,0,1,0,0,math.sin(p),0,math.cos(p),0,0,0,0,1]
 scale=matrix((-1 if i%2 else 1)*(.2+i%7*.3),.3+i%5*.4,.5+i%11*.1)
 m=mul(mul(rz,ry),scale);m[12:15]=[f(i%9-4),f(i%5-2),f(i%7-3)]
 if i%4==0:
  sh=I.copy();sh[2]=.17;m=mul(m,sh)
 source=i%3==0;effective=mul(m,matrix(2,2,2,(2,1,0))) if source else m
 surface=[sum(effective[k*4+r]*[.2,.07,.5][k] for k in range(3))+effective[12+r] for r in range(3)]
 n=cross(effective[:3],effective[4:7]);n=[v/math.hypot(*n) for v in n]
 determinant=dot(effective[:3],cross(effective[4:7],effective[8:11]));out=[v*(1 if determinant>0 else -1) for v in n]
 origin=[f(v+d*10) for v,d in zip(surface,out)];factor=[1e-20,.3,1e20][i%3];direction=[f(-v*factor) for v in out];unit=[v/math.hypot(*direction) for v in direction]
 distance=dot([v-o for v,o in zip(surface,origin)],n)/dot(unit,n);point=[o+d*distance for o,d in zip(origin,unit)]
 add('world-plane-oracle-'+str(i),[instance(world=m)],origin=origin,direction=direction,source=source,expected=hit('front',point,n,distance))
Path('tests/fixtures/picking.json').write_text(json.dumps(cases,indent=2)+'\n')
print(len(cases),'shared picking fixtures')
