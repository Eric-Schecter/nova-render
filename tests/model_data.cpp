#include "ModelData.hpp"
#include "OrbitInput.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
int main(int argc,char** argv){
    try{
        if(argc!=2)return 1;
        const auto model=render::detail::LoadGLB(argv[1]);
        if(model->vertices.empty()||model->vertices.size()%3)throw std::runtime_error("Invalid geometry");
        for(const auto& v:model->vertices)for(int k=0;k<3;++k)if(!std::isfinite(v.position[k])||std::abs(v.position[k])>1.001f)throw std::runtime_error("Not normalized");
        for(const auto& image:model->images)if(image.pixels.size()!=static_cast<size_t>(image.width)*image.height*4)throw std::runtime_error("Invalid texture");
        render::OrbitCamera camera;const auto first=render::detail::MakeSceneUniforms(*model,camera,1.3f);camera.yaw=45;
        const auto second=render::detail::MakeSceneUniforms(*model,camera,1.3f);
        if(std::memcmp(first.viewProjection,second.viewProjection,64)==0)throw std::runtime_error("Camera did not move");
        const auto lit=render::detail::MakeSceneUniforms(*model,camera,1.3f,render::LightingMode::PBR);
        if(first.emissive[3]!=0 || lit.emissive[3]!=1)throw std::runtime_error("Lighting mode uniform mismatch");
        render::DirectionalLight light;light.direction={0,3,4};light.color={0.2f,0.6f,0.9f};light.intensity=8;
        render::detail::ValidateDirectionalLight(light);
        const auto custom=render::detail::MakeSceneUniforms(*model,camera,1.3f,render::LightingMode::PBR,light);
        if(std::abs(custom.lightDirection[1]-0.6f)>1e-6f || std::abs(custom.lightDirection[2]-0.8f)>1e-6f || custom.lightColorIntensity[0]!=0.2f || custom.lightColorIntensity[3]!=8)
            throw std::runtime_error("Light direction/color/intensity upload mismatch");
        auto rejectLight=[](const render::DirectionalLight& bad){bool rejected=false;try{render::detail::ValidateDirectionalLight(bad);}catch(const std::invalid_argument&){rejected=true;}if(!rejected)throw std::runtime_error("Invalid directional light accepted");};
        auto bad=light;bad.direction={0,0,0};rejectLight(bad);
        bad=light;bad.direction[0]=std::numeric_limits<float>::infinity();rejectLight(bad);
        bad=light;bad.color[1]=-0.1f;rejectLight(bad);
        bad=light;bad.color[2]=1.1f;rejectLight(bad);
        bad=light;bad.intensity=-1;rejectLight(bad);
        bad=light;bad.intensity=101;rejectLight(bad);
        bad=light;bad.intensity=std::numeric_limits<float>::quiet_NaN();rejectLight(bad);
        light.intensity=0;render::detail::ValidateDirectionalLight(light);
        const auto dragged=render::detail::DragOrbit(camera,100,1000);
        if(dragged.yaw==camera.yaw || dragged.pitch!=85)throw std::runtime_error("Orbit drag/clamp failed");
        auto zoom=render::detail::ZoomOrbit(camera,-1000);
        if(zoom.distance>=camera.distance)throw std::runtime_error("Wheel zoom direction failed");
        for(int i=0;i<20;++i)zoom=render::detail::ZoomOrbit(zoom,1000);
        if(zoom.distance!=100)throw std::runtime_error("Wheel zoom maximum failed");
        for(int i=0;i<20;++i)zoom=render::detail::ZoomOrbit(zoom,-1000);
        if(zoom.distance!=1.5f)throw std::runtime_error("Wheel zoom minimum failed");
        camera.distance=0;bool rejected=false;try{render::detail::ValidateCamera(camera);}catch(const std::invalid_argument&){rejected=true;}
        if(!rejected)throw std::runtime_error("Invalid camera accepted");
        std::cout<<"Model loading, texture bounds, normalization and camera checks passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
