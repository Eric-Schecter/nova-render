const identity=()=>[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]
export function multiply(a,b){const out=Array(16).fill(0);for(let c=0;c<4;c++)for(let r=0;r<4;r++)for(let k=0;k<4;k++)out[c*4+r]+=a[k*4+r]*b[c*4+k];return out}
const cross=(a,b)=>[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
const dot=(a,b)=>a.reduce((s,x,i)=>s+x*b[i],0)
const normalize=a=>{const n=Math.hypot(...a);if(!n)throw Error('Degenerate model normal');return a.map(x=>x/n)}
function nodeMatrix(n){
  if(n.matrix)return n.matrix
  const [x,y,z,w]=n.rotation||[0,0,0,1],s=n.scale||[1,1,1],t=n.translation||[0,0,0]
  return [(1-2*y*y-2*z*z)*s[0],(2*x*y+2*z*w)*s[0],(2*x*z-2*y*w)*s[0],0,(2*x*y-2*z*w)*s[1],(1-2*x*x-2*z*z)*s[1],(2*y*z+2*x*w)*s[1],0,(2*x*z+2*y*w)*s[2],(2*y*z-2*x*w)*s[2],(1-2*x*x-2*y*y)*s[2],0,...t,1]
}
export async function loadGLB(url){
  const response=await fetch(url);if(!response.ok)throw Error('Load GLB: HTTP '+response.status)
  const bytes=await response.arrayBuffer(),dv=new DataView(bytes)
  if(bytes.byteLength<20||dv.getUint32(0,true)!==0x46546c67||dv.getUint32(4,true)!==2||dv.getUint32(8,true)!==bytes.byteLength)throw Error('Expected glTF 2.0 GLB')
  let json,bin
  for(let offset=12;offset<bytes.byteLength;){if(offset+8>bytes.byteLength)throw Error('Truncated GLB');const size=dv.getUint32(offset,true),type=dv.getUint32(offset+4,true);offset+=8;if(offset+size>bytes.byteLength)throw Error('Invalid GLB chunk');if(type===0x4e4f534a)json=JSON.parse(new TextDecoder().decode(new Uint8Array(bytes,offset,size)));if(type===0x004e4942)bin=bytes.slice(offset,offset+size);offset+=size}
  if(!json||!bin||json.extensionsRequired?.length||json.extensionsUsed?.length||json.skins?.length||json.animations?.length||json.buffers.length!==1||json.buffers[0].uri)throw Error('Only static core GLB with embedded data is supported')
  const raw=new DataView(bin)
  function element(index,i,type,components){
    const a=json.accessors[index],v=json.bufferViews[a.bufferView]
    const size={5121:1,5123:2,5125:4,5126:4}[a.componentType],stride=v.byteStride||size*components
    const start=(v.byteOffset||0)+(a.byteOffset||0)+i*stride
    if(a.sparse||a.normalized||a.type!==type||i>=a.count||!size||v.buffer!==0||start+size*components>(v.byteOffset||0)+v.byteLength||start+size*components>bin.byteLength)throw Error('Unsupported or out-of-bounds accessor')
    return Array.from({length:components},(_,k)=>a.componentType===5126?raw.getFloat32(start+k*size,true):a.componentType===5125?raw.getUint32(start+k*size,true):a.componentType===5123?raw.getUint16(start+k*size,true):raw.getUint8(start+k))
  }
  const vertices=[],min=[Infinity,Infinity,Infinity],max=[-Infinity,-Infinity,-Infinity],active=new Set();let materialIndex=-1
  function visit(index,parent){
    if(!json.nodes[index]||active.has(index))throw Error('Invalid scene graph');active.add(index)
    const node=json.nodes[index],m=multiply(parent,nodeMatrix(node)),a=m.slice(0,3),b=m.slice(4,7),c=m.slice(8,11)
    const cof=[cross(b,c),cross(c,a),cross(a,b)],det=dot(a,cof[0]);if(Math.abs(det)<1e-10)throw Error('Degenerate node transform')
    if(node.mesh!==undefined)for(const p of json.meshes[node.mesh].primitives){
      if((p.mode??4)!==4||p.indices===undefined||p.targets)throw Error('Only indexed static triangle primitives are supported')
      if(materialIndex<0)materialIndex=p.material
      if(p.material===undefined||p.material!==materialIndex)throw Error('Initial model loader requires one material')
      if(![5121,5123,5125].includes(json.accessors[p.indices].componentType))throw Error('Invalid index format')
      const count=json.accessors[p.indices].count;if(count%3||vertices.length/12+count>10000000)throw Error('Invalid or oversized mesh')
      for(let i=0;i<count;i++){
        const index=element(p.indices,i,'SCALAR',1)[0],pos=element(p.attributes.POSITION,index,'VEC3',3),normal=element(p.attributes.NORMAL,index,'VEC3',3),uv=element(p.attributes.TEXCOORD_0,index,'VEC2',2)
        const world=pos.map((_,r)=>m[r]*pos[0]+m[4+r]*pos[1]+m[8+r]*pos[2]+m[12+r])
        const n=normalize(normal.map((_,r)=>(cof[0][r]*normal[0]+cof[1][r]*normal[1]+cof[2][r]*normal[2])/det))
        if(![...world,...n,...uv].every(Number.isFinite))throw Error('Non-finite model data')
        for(let k=0;k<3;k++){min[k]=Math.min(min[k],world[k]);max[k]=Math.max(max[k],world[k])}
        vertices.push(...world,1,...n,0,...uv,0,0)
      }
    }
    for(const child of node.children||[])visit(child,m);active.delete(index)
  }
  for(const root of json.scenes[json.scene||0].nodes)visit(root,identity())
  const size=Math.max(...max.map((x,i)=>x-min[i]));if(!vertices.length||!Number.isFinite(size)||size<=0)throw Error('Empty model')
  for(let i=0;i<vertices.length;i+=12)for(let k=0;k<3;k++)vertices[i+k]=(vertices[i+k]-(min[k]+max[k])*0.5)*2/size
  const material=json.materials[materialIndex],pbr=material.pbrMetallicRoughness||{}
  if((material.alphaMode||'OPAQUE')!=='OPAQUE')throw Error('Only opaque materials are supported')
  const textures=[pbr.baseColorTexture,material.normalTexture,pbr.metallicRoughnessTexture,material.occlusionTexture,material.emissiveTexture]
  const fallback=[[255,255,255,255],[128,128,255,255],[255,255,255,255],[255,255,255,255],[0,0,0,255]]
  const images=[]
  try {
    for(let i=0;i<5;i++){
      if(!textures[i]){images.push({width:1,height:1,pixels:new Uint8Array(fallback[i])});continue}
      if((textures[i].texCoord||0)!==0)throw Error('Only TEXCOORD_0 is supported')
      const image=json.images[json.textures[textures[i].index].source]
      if(image.bufferView===undefined)throw Error('GLB images must be embedded')
      const view=json.bufferViews[image.bufferView],offset=view.byteOffset||0
      if(offset+view.byteLength>bin.byteLength)throw Error('Image out of bounds')
      const bitmap=await createImageBitmap(new Blob([bin.slice(offset,offset+view.byteLength)],{type:image.mimeType}),{colorSpaceConversion:'none',premultiplyAlpha:'none'})
      if(bitmap.width>8192||bitmap.height>8192){bitmap.close();throw Error('Image exceeds 8192 pixels')}
      images.push({width:bitmap.width,height:bitmap.height,bitmap})
    }
    return {vertices:new Float32Array(vertices),images,baseColor:pbr.baseColorFactor||[1,1,1,1],material:[pbr.metallicFactor??1,pbr.roughnessFactor??1,material.normalTexture?.scale??1,material.occlusionTexture?.strength??1],emissive:[...(material.emissiveFactor||[0,0,0]),0]}
  }catch(e){for(const image of images)image.bitmap?.close();throw e}
}
export function validateCamera(c){if(![c.yaw,c.pitch,c.distance,c.exposure].every(Number.isFinite)||Math.abs(c.pitch)>85||c.distance<1.5||c.distance>100||c.exposure<=0||c.exposure>16)throw Error('Invalid orbit camera (pitch +/-85, distance 1.5..100, exposure 0..16)')}
export function sceneUniforms(data,camera,aspect){
  const yaw=camera.yaw*Math.PI/180,pitch=camera.pitch*Math.PI/180,d=camera.distance
  const eye=[d*Math.cos(pitch)*Math.sin(yaw),d*Math.sin(pitch),d*Math.cos(pitch)*Math.cos(yaw)]
  const f=normalize(eye.map(x=>-x)),s=normalize(cross(f,[0,1,0])),u=cross(s,f)
  const view=[s[0],u[0],-f[0],0,s[1],u[1],-f[1],0,s[2],u[2],-f[2],0,-dot(s,eye),-dot(u,eye),dot(f,eye),1]
  const t=1/Math.tan(Math.PI/8),near=0.01,far=200
  const projection=[t/aspect,0,0,0,0,t,0,0,0,0,far/(near-far),-1,0,0,near*far/(near-far),0]
  return new Float32Array([...multiply(projection,view),...eye,camera.exposure,...data.baseColor,...data.material,...data.emissive])
}
