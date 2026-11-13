#pragma once
#include <render/Window.hpp>
#include <stdexcept>
#include <string>

namespace example {
struct ModelOptions {
    render::WindowConfig window;
    std::string modelPath;
    render::DirectionalLight light;
    int frameLimit = 0;
    bool spin = false; // Optional automatic orbit for fixed-frame validation.
};

inline ModelOptions ParseModelOptions(int argc, char** argv, const char* title, bool allowLighting = false) {
    if (argc < 2) throw std::invalid_argument(
        "Usage: example model.glb [--api metal|vulkan|dx12] [--warp] [--frames N] [--spin] "
        "[--light-direction X Y Z] [--light-color R G B] [--light-intensity N]");
    ModelOptions options;
    options.modelPath = argv[1];
    options.window.title = title;
    options.window.width = 960;
    options.window.height = 720;
        auto number=[](const char* text){size_t used=0;const std::string value=text;const float n=std::stof(value,&used);if(used!=value.size())throw std::invalid_argument("Invalid light number");return n;};

        for(int i=2;i<argc;++i){const std::string arg=argv[i];
            if(arg=="--api"&&i+1<argc){const std::string api=argv[++i];
                if(api=="metal")options.window.renderer.api=render::GraphicsAPI::Metal;
                else if(api=="vulkan")options.window.renderer.api=render::GraphicsAPI::Vulkan;
                else if(api=="dx12")options.window.renderer.api=render::GraphicsAPI::D3D12;
                else throw std::invalid_argument("Unknown API");
            }else if(arg=="--frames"&&i+1<argc){size_t consumed=0;const std::string n=argv[++i];options.frameLimit=std::stoi(n,&consumed);if(options.frameLimit<=0||consumed!=n.size())throw std::invalid_argument("Invalid frame count");}
            else if(allowLighting&&(arg=="--light-direction"||arg=="--light-color")&&i+3<argc){
                auto& values=arg=="--light-direction"?options.light.direction:options.light.color;
                for(float& value:values)value=number(argv[++i]);
            }
            else if(allowLighting&&arg=="--light-intensity"&&i+1<argc)options.light.intensity=number(argv[++i]);
            else if(arg=="--warp")options.window.renderer.useWarp=true;
            else if(arg=="--spin")options.spin=true;
            else throw std::invalid_argument("Unknown argument: "+arg);
        }
    return options;
}
} // namespace example
