#include <render/Window.hpp>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc,char** argv) {
    try {
        if(argc<2)throw std::runtime_error("Usage: nova_helmet model.glb [--api metal|vulkan|dx12] [--warp] [--frames N] [--spin]");
        render::WindowConfig config;config.title="Nova | Damaged Helmet";config.width=960;config.height=720;
        int limit=0;bool spin=false;
        for(int i=2;i<argc;++i){const std::string arg=argv[i];
            if(arg=="--api"&&i+1<argc){const std::string api=argv[++i];
                if(api=="metal")config.renderer.api=render::GraphicsAPI::Metal;
                else if(api=="vulkan")config.renderer.api=render::GraphicsAPI::Vulkan;
                else if(api=="dx12")config.renderer.api=render::GraphicsAPI::D3D12;
                else throw std::invalid_argument("Unknown API");
            }else if(arg=="--frames"&&i+1<argc){size_t consumed=0;const std::string n=argv[++i];limit=std::stoi(n,&consumed);if(limit<=0||consumed!=n.size())throw std::invalid_argument("Invalid frame count");}
            else if(arg=="--warp")config.renderer.useWarp=true;
            else if(arg=="--spin")spin=true;
            else throw std::invalid_argument("Unknown argument: "+arg);
        }
        render::Window window(config);
        window.LoadModel(argv[1]);
        render::OrbitCamera camera;camera.yaw=20;camera.pitch=5;
        window.SetCamera(camera);
        window.EnableOrbitControls();
        int frames=0;
        while((!limit||frames<limit)&&window.PollEvents()){
            if(spin){camera.yaw=20.0f+frames*0.25f;window.SetCamera(camera);}
            if(window.DrawFrame()==render::FrameResult::Rendered)++frames;
        }
        window.WaitIdle();
        std::cout<<"Rendered "<<frames<<" frames; validation errors: "<<window.ValidationErrorCount()<<'\n';
        return window.ValidationErrorCount()==0&&(!limit||frames==limit)?0:1;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
