#include "ModelData.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
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
        camera.distance=0;bool rejected=false;try{render::detail::ValidateCamera(camera);}catch(const std::invalid_argument&){rejected=true;}
        if(!rejected)throw std::runtime_error("Invalid camera accepted");
        std::cout<<"Model loading, texture bounds, normalization and camera checks passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
