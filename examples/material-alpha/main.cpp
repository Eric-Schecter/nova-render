#include "../common/ModelOptions.hpp"
#include "../common/PerformancePanel.hpp"
#include <iostream>
#ifdef NOVA_EXAMPLE_UI
#include <imgui.h>
#endif
int main(int argc, char** argv)
{
    try
    {
        auto           options = example::ParseModelOptions(argc, argv, "Nova Render | Alpha materials", true);
        render::Window window(options.window);
        window.LoadModel(options.modelPath);
        window.SetLighting(render::LightingMode::Unlit);
        window.SetAntiAliasing({render::AAMethod::FXAA});
        window.SetViewCamera(render::Camera{});
        window.EnableOrbitControls();
        example::PerformancePanel    performance;
        render::ParameterPanelConfig panel;
        panel.showBuiltinPanel = false;
        bool dirty             = false;
        int  slot = 0, mode = 0, pipeline = 0, lighting = 0;
        auto material = window.GetMaterials().at(0);
        panel.drawUI  = [&] {
#ifdef NOVA_EXAMPLE_UI
            if (ImGui::Begin("Materials"))
            {
                auto materials = window.GetMaterials();
                if (ImGui::SliderInt("Material slot", &slot, 0, int(materials.size()) - 1))
                {
                    material = materials[slot];
                    mode     = int(material.alphaMode);
                }
                dirty |= ImGui::Combo("Alpha mode", &mode, "OPAQUE\0MASK\0BLEND\0");
                dirty |= ImGui::SliderFloat("Opacity", &material.baseColor[3], 0, 1);
                dirty |= ImGui::SliderFloat("Alpha cutoff", &material.alphaCutoff, 0, 1);
                dirty |= ImGui::Checkbox("Double sided", &material.doubleSided);
                dirty |= ImGui::Combo("Raster pipeline", &pipeline, "Forward\0Deferred\0");
                dirty |= ImGui::Combo("Lighting", &lighting, "Unlit\0PBR\0");
            }
            ImGui::End();
#endif
        };
        performance.Attach(panel);
        window.EnableParameterPanel(panel);
        int frames = 0;
        while ((!options.frameLimit || frames < options.frameLimit) && window.PollEvents())
        {
            if (performance.DrawFrame(window, window.GetFramebufferSize()) == render::FrameResult::Rendered)
                ++frames;
            if (dirty)
            {
                material.alphaMode = render::AlphaMode(mode);
                window.SetMaterial(slot, material);
                render::RasterSettings raster;
                raster.pipeline = pipeline ? render::RasterPipeline::Deferred : render::RasterPipeline::Forward;
                window.SetRasterSettings(raster);
                window.SetLighting(lighting ? render::LightingMode::PBR : render::LightingMode::Unlit);
                dirty = false;
            }
        }
        window.WaitIdle();
        return window.ValidationErrorCount() ? 1 : 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
