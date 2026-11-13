#include "ModelData.hpp"
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace render::detail {
namespace {
// The initial loader deliberately supports static, opaque, single-material GLBs.
const unsigned char* element(const tinygltf::Model& model, int accessorIndex, size_t index, int type, int component) {
    const auto& a=model.accessors.at(accessorIndex);
    if(a.sparse.isSparse || a.type!=type || a.componentType!=component || index>=a.count || a.bufferView<0)
        throw std::runtime_error("Unsupported glTF accessor");
    const auto& view=model.bufferViews.at(a.bufferView);
    const auto& data=model.buffers.at(view.buffer).data;
    const int stride=a.ByteStride(view);
    const size_t size=tinygltf::GetComponentSizeInBytes(component)*tinygltf::GetNumComponentsInType(type);
    if(stride<=0 || view.byteOffset>data.size() || view.byteLength>data.size()-view.byteOffset ||
       a.byteOffset>view.byteLength || index>(view.byteLength-a.byteOffset)/static_cast<size_t>(stride))
        throw std::runtime_error("Invalid glTF buffer bounds");
    const size_t offset=a.byteOffset+index*static_cast<size_t>(stride);
    if(offset>view.byteLength || size>view.byteLength-offset) throw std::runtime_error("Truncated glTF accessor");
    return data.data()+view.byteOffset+offset;
}
glm::vec3 vec3(const tinygltf::Model& m,int a,size_t i){glm::vec3 v;std::memcpy(&v,element(m,a,i,TINYGLTF_TYPE_VEC3,TINYGLTF_COMPONENT_TYPE_FLOAT),12);return v;}
glm::vec2 vec2(const tinygltf::Model& m,int a,size_t i){glm::vec2 v;std::memcpy(&v,element(m,a,i,TINYGLTF_TYPE_VEC2,TINYGLTF_COMPONENT_TYPE_FLOAT),8);return v;}
}
std::shared_ptr<const ModelData> LoadGLB(const std::filesystem::path& path) {
    tinygltf::Model gltf; tinygltf::TinyGLTF loader; std::string errors,warnings;
    if(!loader.LoadBinaryFromFile(&gltf,&errors,&warnings,path.string())) throw std::runtime_error("Load GLB: "+path.string()+": "+errors);
    if(!gltf.extensionsRequired.empty() || !gltf.extensionsUsed.empty() || !gltf.skins.empty() || !gltf.animations.empty()) throw std::runtime_error("Only static core glTF GLB is supported");
    auto result=std::make_shared<ModelData>();
    int materialIndex=-1; glm::vec3 minimum(std::numeric_limits<float>::max()),maximum(-std::numeric_limits<float>::max());
    std::vector<bool> active(gltf.nodes.size(),false);
    std::function<void(int,glm::mat4)> visit=[&](int index,glm::mat4 parent){
        if(index<0 || static_cast<size_t>(index)>=gltf.nodes.size() || active[index])throw std::runtime_error("Invalid glTF scene graph");
        active[index]=true;
        const auto& node=gltf.nodes.at(index); glm::mat4 local(1);
        if(node.matrix.size()==16){for(int i=0;i<16;++i)glm::value_ptr(local)[i]=static_cast<float>(node.matrix[i]);}
        else {
            if(node.translation.size()==3)local=glm::translate(local,glm::vec3(node.translation[0],node.translation[1],node.translation[2]));
            if(node.rotation.size()==4)local*=glm::mat4_cast(glm::quat(node.rotation[3],node.rotation[0],node.rotation[1],node.rotation[2]));
            if(node.scale.size()==3)local=glm::scale(local,glm::vec3(node.scale[0],node.scale[1],node.scale[2]));
        }
        const auto matrix=parent*local;const auto normals=glm::transpose(glm::inverse(glm::mat3(matrix)));
        if(node.mesh>=0)for(const auto& p:gltf.meshes.at(node.mesh).primitives){
            if(p.mode!=TINYGLTF_MODE_TRIANGLES || p.indices<0 || !p.targets.empty())throw std::runtime_error("Only indexed static triangle primitives are supported");
            if(materialIndex==-1)materialIndex=p.material;
            if(p.material<0 || materialIndex!=p.material)throw std::runtime_error("The initial GLB loader requires one material");
            const int position=p.attributes.at("POSITION"),normal=p.attributes.at("NORMAL"),uv=p.attributes.at("TEXCOORD_0");
            const auto& indices=gltf.accessors.at(p.indices);
            if(indices.count%3 || result->vertices.size()+indices.count>10000000)throw std::runtime_error("Invalid or oversized triangle mesh");
            for(size_t i=0;i<indices.count;++i){
                const int c=indices.componentType;uint32_t v=0;
                if(c!=TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE && c!=TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT && c!=TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)throw std::runtime_error("Invalid index format");
                std::memcpy(&v,element(gltf,p.indices,i,TINYGLTF_TYPE_SCALAR,c),tinygltf::GetComponentSizeInBytes(c));
                const glm::vec3 pos=glm::vec3(matrix*glm::vec4(vec3(gltf,position,v),1));
                const glm::vec3 n=glm::normalize(normals*vec3(gltf,normal,v));const glm::vec2 t=vec2(gltf,uv,v);
                for(int k=0;k<3;++k)if(!std::isfinite(pos[k])||!std::isfinite(n[k]))throw std::runtime_error("Non-finite model data");
                if(!std::isfinite(t.x)||!std::isfinite(t.y))throw std::runtime_error("Non-finite texture coordinates");
                minimum=glm::min(minimum,pos);maximum=glm::max(maximum,pos);
                result->vertices.push_back({{pos.x,pos.y,pos.z,1},{n.x,n.y,n.z,0},{t.x,t.y,0,0}});
            }
        }
        for(int child:node.children)visit(child,matrix);
        active[index]=false;
    };
    const int scene=gltf.defaultScene>=0?gltf.defaultScene:0;
    for(int root:gltf.scenes.at(scene).nodes)visit(root,glm::mat4(1));
    if(result->vertices.empty())throw std::runtime_error("GLB contains no triangles");
    const auto center=(minimum+maximum)*0.5f;const auto extent=maximum-minimum;const float size=std::max({extent.x,extent.y,extent.z});
    if(!(size>0))throw std::runtime_error("Degenerate GLB bounds");
    for(auto& v:result->vertices)for(int k=0;k<3;++k)v.position[k]=(v.position[k]-center[k])*2.0f/size;
    const auto& mat=gltf.materials.at(materialIndex);const auto& pbr=mat.pbrMetallicRoughness;
    if(mat.alphaMode!="OPAQUE")throw std::runtime_error("Only opaque materials are supported");
    for(int k=0;k<4;++k)result->baseColor[k]=static_cast<float>(pbr.baseColorFactor[k]);
    result->material={static_cast<float>(pbr.metallicFactor),static_cast<float>(pbr.roughnessFactor),static_cast<float>(mat.normalTexture.scale),static_cast<float>(mat.occlusionTexture.strength)};
    for(int k=0;k<3;++k)result->emissive[k]=static_cast<float>(mat.emissiveFactor[k]);
    if(pbr.baseColorTexture.texCoord || mat.normalTexture.texCoord || pbr.metallicRoughnessTexture.texCoord || mat.occlusionTexture.texCoord || mat.emissiveTexture.texCoord)throw std::runtime_error("Only TEXCOORD_0 is supported");
    const int textures[]={pbr.baseColorTexture.index,mat.normalTexture.index,pbr.metallicRoughnessTexture.index,mat.occlusionTexture.index,mat.emissiveTexture.index};
    const std::array<std::array<unsigned char,4>,5> fallback={{{255,255,255,255},{128,128,255,255},{255,255,255,255},{255,255,255,255},{0,0,0,255}}};
    for(int i=0;i<5;++i){auto& dst=result->images[i];dst.pixels.assign(fallback[i].begin(),fallback[i].end());if(textures[i]<0)continue;
        const auto& image=gltf.images.at(gltf.textures.at(textures[i]).source);
        if(image.width<=0||image.height<=0||image.width>8192||image.height>8192||image.bits!=8||image.component!=4||image.image.size()!=static_cast<size_t>(image.width)*image.height*4)throw std::runtime_error("Expected RGBA8 glTF image");
        dst.width=image.width;dst.height=image.height;dst.pixels=image.image;
    }
    std::cout<<"Model: "<<path.filename().string()<<"; triangles: "<<result->vertices.size()/3<<'\n';
    return result;
}
void ValidateCamera(const OrbitCamera& c){
    if(!std::isfinite(c.yaw)||!std::isfinite(c.pitch)||!std::isfinite(c.distance)||!std::isfinite(c.exposure)||std::abs(c.pitch)>85||c.distance<1.5f||c.distance>100||c.exposure<=0||c.exposure>16)
        throw std::invalid_argument("Invalid orbit camera (pitch +/-85, distance 1.5..100, exposure 0..16)");
}
SceneUniforms MakeSceneUniforms(const ModelData& data,const OrbitCamera& camera,float aspect){
    const float yaw=glm::radians(camera.yaw),pitch=glm::radians(camera.pitch);
    const glm::vec3 eye(camera.distance*std::cos(pitch)*std::sin(yaw),camera.distance*std::sin(pitch),camera.distance*std::cos(pitch)*std::cos(yaw));
    const auto vp=glm::perspectiveRH_ZO(glm::radians(45.0f),aspect,0.01f,200.0f)*glm::lookAtRH(eye,glm::vec3(0),glm::vec3(0,1,0));
    SceneUniforms result{};std::memcpy(result.viewProjection,glm::value_ptr(vp),64);
    result.eyeExposure[0]=eye.x;result.eyeExposure[1]=eye.y;result.eyeExposure[2]=eye.z;result.eyeExposure[3]=camera.exposure;
    std::copy(data.baseColor.begin(),data.baseColor.end(),result.baseColor);std::copy(data.material.begin(),data.material.end(),result.material);std::copy(data.emissive.begin(),data.emissive.end(),result.emissive);
    return result;
}
}
