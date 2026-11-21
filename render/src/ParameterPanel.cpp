#include "ParameterPanel.hpp"
#include "Color.hpp"
#ifdef NOVA_HAS_UI
#include <algorithm>
#include <cmath>
#include <imgui_impl_sdl3.h>
#include <stdexcept>

namespace render::detail
{
    ParameterPanel::ParameterPanel(PresentationWindow value)
      : window(value)
    {
        IMGUI_CHECKVERSION();
        context = ImGui::CreateContext();
        Activate();
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::GetIO().LogFilename = nullptr;
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();
        auto& style                          = ImGui::GetStyle();
        style.WindowRounding                 = 6;
        style.FrameRounding                  = 4;
        style.WindowPadding                  = {10, 10};
        style.Colors[ImGuiCol_WindowBg]      = {0.094f, 0.118f, 0.16f, 0.96f};
        style.Colors[ImGuiCol_TitleBg]       = {0.145f, 0.196f, 0.294f, 1};
        style.Colors[ImGuiCol_TitleBgActive] = style.Colors[ImGuiCol_TitleBg];
        style.Colors[ImGuiCol_SliderGrab]    = {0.506f, 0.62f, 1, 1};
        style.Colors[ImGuiCol_Text]          = {0.882f, 0.902f, 0.941f, 1};
        style.Colors[ImGuiCol_Header]        = {0.145f, 0.196f, 0.294f, 1};
        style.Colors[ImGuiCol_FrameBg]       = {0.145f, 0.196f, 0.294f, 1};
        style.Colors[ImGuiCol_Button]        = {0.16f, 0.212f, 0.306f, 1};
        style.Colors[ImGuiCol_Border]        = {0.235f, 0.275f, 0.349f, 1};
        std::copy(style.Colors, style.Colors + ImGuiCol_COUNT, displayColors.begin());
        initialized = ImGui_ImplSDL3_InitForOther(window.sdl);
        if (!initialized)
        {
            ImGui::DestroyContext(context);
            context = nullptr;
            throw std::runtime_error("Initialize parameter UI input failed");
        }
    }
    void ParameterPanel::ConfigureUI(const std::function<void()>& configure)
    {
        if (applicationConfigured || !configure)
            return;
        Activate();
        configure();
        auto& style = ImGui::GetStyle();
        std::copy(style.Colors, style.Colors + ImGuiCol_COUNT, displayColors.begin());
        applicationConfigured = true;
    }
    ParameterPanel::~ParameterPanel()
    {
        if (context)
        {
            Activate();
            if (initialized)
                ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext(context);
        }
    }
    void ParameterPanel::ProcessWindowEvent(const void* event)
    {
        Activate();
        ImGui_ImplSDL3_ProcessEvent(static_cast<const SDL_Event*>(event));
    }
    void ParameterPanel::Activate() const
    {
        ImGui::SetCurrentContext(context);
    }
    void ParameterPanel::CaptureDefaults(
      const OrbitCamera& camera, const DirectionalLight& light, const EnvironmentSettings& environment)
    {
        initialCamera      = camera;
        initialLight       = light;
        initialEnvironment = environment;
    }
    void ParameterPanel::SetSRGBOutput(bool enabled)
    {
        Activate();
        auto& style = ImGui::GetStyle();
        for (int i = 0; i < ImGuiCol_COUNT; ++i)
        {
            auto color = displayColors[i];
            if (enabled)
            {
                color.x = SRGBToLinear(color.x);
                color.y = SRGBToLinear(color.y);
                color.z = SRGBToLinear(color.z);
            }
            style.Colors[i] = color;
        }
    }
    bool ParameterPanel::WantsMouse() const
    {
        Activate();
        double x, y;
        window.CursorPosition(x, y);
        return ImGui::GetIO().WantCaptureMouse
               || (x >= position.x && y >= position.y && x < position.x + size.x && y < position.y + size.y);
    }
    bool ParameterPanel::WantsKeyboard() const
    {
        Activate();
        return ImGui::GetIO().WantCaptureKeyboard;
    }
    void ParameterPanel::Draw(
      bool                              model,
      LightingMode                      lighting,
      OrbitCamera&                      camera,
      DirectionalLight&                 light,
      std::array<float, 4>&             clear,
      bool                              hasEnvironment,
      EnvironmentSettings&              environment,
      PanelAssets&                      assets,
      RenderMode                        mode,
      const RendererCapabilities&       capabilities,
      PathTracingSettings&              path,
      uint32_t                          samples,
      const std::vector<AnimationClip>& clips,
      AnimationState&                   animation,
      AntiAliasingSettings&             aa,
      RasterSettings&                   raster,
      HybridSettings&                   hybrid,
      const HybridCapabilities&         hybridCaps)
    {
        Activate();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        if (assets.config.drawUI)
            assets.config.drawUI();
        if (!assets.config.showBuiltinPanel)
        {
            position = {};
            size     = {};
            ImGui::Render();
            return;
        }
        const auto  display = ImGui::GetIO().DisplaySize;
        const float width   = std::min(250.0f, std::max(80.0f, display.x - 24));
        ImGui::SetNextWindowPos(
          {std::max(0.0f, display.x - width - 12), 12 + assets.config.sceneTopInset}, ImGuiCond_Always);
        ImGui::SetNextWindowSizeConstraints(
          {width, 0}, {width, std::max(40.0f, display.y - 24 - assets.config.sceneTopInset)});
        ImGui::SetNextWindowCollapsed(display.x < 520, ImGuiCond_Once);
        const bool open = ImGui::Begin(
          "Parameters",
          nullptr,
          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_AlwaysAutoResize);
        position = ImGui::GetWindowPos();
        size     = ImGui::GetWindowSize();
        if (open)
        {
            ImGui::BeginDisabled(mode == RenderMode::RayTracing);
            const char* aaNames[] = {"Off", "FXAA", "SMAA 1x", "MSAA 4x", "SSAA 2x2", "TAA"};
            if (ImGui::BeginCombo("Anti-aliasing", aaNames[unsigned(aa.method)]))
            {
                for (int i = 0; i < 6; i++)
                {
                    ImGui::BeginDisabled(
                      (i == 3 && raster.pipeline == RasterPipeline::Deferred)
                      || (i == 5 && mode == RenderMode::Hybrid));
                    if (ImGui::Selectable(aaNames[i], unsigned(aa.method) == unsigned(i)))
                        aa.method = AAMethod(i);
                    ImGui::EndDisabled();
                }
                ImGui::EndCombo();
            }
            if (model)
            {
                const char* pipelines[] = {"Forward", "Deferred"};
                if (ImGui::BeginCombo("Raster pipeline", pipelines[unsigned(raster.pipeline)]))
                {
                    for (int i = 0; i < 2; i++)
                    {
                        ImGui::BeginDisabled(
                          (i == 1 && aa.method == AAMethod::MSAA) || (i == 0 && mode == RenderMode::Hybrid));
                        if (ImGui::Selectable(pipelines[i], unsigned(raster.pipeline) == unsigned(i)))
                        {
                            raster.pipeline = RasterPipeline(i);
                            raster.view     = GBufferView::Final;
                        }
                        ImGui::EndDisabled();
                    }
                    ImGui::EndCombo();
                }
                if (raster.pipeline == RasterPipeline::Deferred)
                {
                    int view = int(raster.view);
                    if (ImGui::Combo(
                          "G-buffer view",
                          &view,
                          "Final lighting\0Base color\0World normal\0Geometric normal\0Linear "
                          "depth\0Roughness\0Metallic\0Occlusion\0Emissive\0World position\0"))
                        raster.view = GBufferView(view);
                    if (raster.view != GBufferView::Final)
                    {
                        ImGui::TextWrapped("Diagnostic: AA and post-processing bypassed");
                        ImGui::SliderFloat("Debug range", &raster.debugRange, .1f, 200);
                    }
                    ImGui::TextDisabled("Deferred MSAA is not supported");
                }
            }
            ImGui::EndDisabled();
            auto chooseAsset = [](
                                 const char*                           label,
                                 const std::vector<AssetChoice>&       choices,
                                 const std::filesystem::path&          current,
                                 std::optional<std::filesystem::path>& requested) {
                if (choices.empty())
                    return;
                std::string selected = current.stem().string();
                for (const auto& choice : choices)
                    if (choice.path == current)
                        selected = choice.name;
                ImGui::TextUnformatted(label);
                ImGui::SetNextItemWidth(-1);
                ImGui::PushID(label);
                if (ImGui::BeginCombo("##choice", selected.c_str()))
                {
                    for (size_t i = 0; i < choices.size(); ++i)
                    {
                        ImGui::PushID(static_cast<int>(i));
                        const bool active = choices[i].path == current;
                        if (ImGui::Selectable(choices[i].name.c_str(), active))
                            requested = choices[i].path;
                        if (active)
                            ImGui::SetItemDefaultFocus();
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopID();
            };
            if (model && ImGui::CollapsingHeader("Renderer", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::TextUnformatted("Mode");
                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo(
                      "##RenderMode",
                      mode == RenderMode::Raster   ? "Raster"
                      : mode == RenderMode::Hybrid ? "Hybrid"
                                                   : "Ray tracing"))
                {
                    if (ImGui::Selectable("Raster", mode == RenderMode::Raster))
                        assets.requestedMode = RenderMode::Raster;
                    ImGui::BeginDisabled(
                      !hybridCaps.supported || aa.method == AAMethod::MSAA || aa.method == AAMethod::TAA || aa.split);
                    if (ImGui::Selectable("Hybrid", mode == RenderMode::Hybrid))
                        assets.requestedMode = RenderMode::Hybrid;
                    ImGui::EndDisabled();
                    ImGui::BeginDisabled(!capabilities.pathTracing);
                    if (ImGui::Selectable("Ray tracing", mode == RenderMode::RayTracing))
                        assets.requestedMode = RenderMode::PathTracing;
                    if (!capabilities.pathTracing && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        ImGui::SetTooltip("%s", capabilities.reason.c_str());
                    ImGui::EndDisabled();
                    ImGui::EndCombo();
                }
                if (mode == RenderMode::Hybrid)
                {
                    ImGui::Checkbox("Hybrid shadows", &hybrid.shadows);
                    int samples = int(hybrid.shadowSamples);
                    if (ImGui::SliderInt("Shadow rays/pixel/light", &samples, 1, 16))
                        hybrid.shadowSamples = uint32_t(samples);
                    ImGui::BeginDisabled(!hybrid.shadows);
                    ImGui::Checkbox("Shadow blue noise", &hybrid.blueNoiseShadows);
                    ImGui::EndDisabled();
                    ImGui::Checkbox("Hybrid ambient occlusion", &hybrid.ambientOcclusion);
                    ImGui::BeginDisabled(!hybrid.ambientOcclusion);
                    ImGui::Checkbox("Half-resolution Hybrid AO", &hybrid.halfResolutionAO);
                    ImGui::BeginDisabled(!hybrid.halfResolutionAO);
                    ImGui::Checkbox("AO blue noise", &hybrid.blueNoiseAO);
                    ImGui::EndDisabled();
                    ImGui::EndDisabled();
                    ImGui::SliderFloat("AO radius", &hybrid.aoRadius, .01f, 10.f);
                    ImGui::SliderFloat("AO strength", &hybrid.aoStrength, 0, 4);
                    int aoSamples = int(hybrid.aoSamples);
                    if (ImGui::SliderInt("AO rays/pixel", &aoSamples, 1, 32))
                        hybrid.aoSamples = uint32_t(aoSamples);
                    ImGui::BeginDisabled(!hybridCaps.probeGI);
                    ImGui::Checkbox("DDGI probes", &hybrid.probeGI);
                    ImGui::BeginDisabled(!hybrid.probeGI);
                    ImGui::Checkbox("DDGI shared rays", &hybrid.probeSharedRays);
                    ImGui::Checkbox("DDGI sky lighting", &hybrid.probeSkyLighting);
                    ImGui::BeginDisabled(!hybrid.probeSkyLighting);
                    ImGui::Checkbox("DDGI multi-bounce", &hybrid.probeMultiBounce);
                    ImGui::BeginDisabled(!hybrid.reflections);
                    ImGui::Checkbox("DDGI reflection lighting", &hybrid.probeReflectionLighting);
                    ImGui::Checkbox("DDGI rough reflections", &hybrid.probeRoughReflections);
                    ImGui::EndDisabled();
                    ImGui::EndDisabled();
                    ImGui::EndDisabled();
                    ImGui::EndDisabled();
                    ImGui::BeginDisabled(hybrid.probeGI);
                    ImGui::Checkbox("Diffuse bounce", &hybrid.diffuseBounce);
                    ImGui::EndDisabled();
                    ImGui::Checkbox("Hybrid reflections", &hybrid.reflections);
                    ImGui::SliderFloat("Reflection distance", &hybrid.reflectionDistance, .1f, 1000.f);
                    ImGui::SliderFloat("Reflection strength", &hybrid.reflectionStrength, 0, 1);
                    ImGui::Checkbox("Half-resolution reflections", &hybrid.halfResolutionReflections);
                    ImGui::BeginDisabled(!hybrid.reflections);
                    ImGui::Checkbox("Mirror reflection shortcut", &hybrid.mirrorReflections);
                    ImGui::Checkbox("Trace clearcoat reflections", &hybrid.clearcoatReflections);
                    ImGui::EndDisabled();
                    ImGui::BeginDisabled(!hybrid.reflections || !hybrid.halfResolutionReflections);
                    ImGui::Checkbox("Reflection blue noise", &hybrid.blueNoiseReflections);
                    ImGui::Checkbox("Reflection temporal denoising", &hybrid.reflectionDenoising);
                    ImGui::BeginDisabled(!hybrid.reflectionDenoising);
                    ImGui::Checkbox("Reflection SVGF", &hybrid.reflectionSVGF);
                    ImGui::EndDisabled();
                    ImGui::EndDisabled();
                    ImGui::BeginDisabled(
                      !hybrid.reflections || !hybrid.halfResolutionReflections || !hybrid.reflectionDenoising);
                    ImGui::Checkbox("Reflection hit reprojection", &hybrid.reflectionHitReprojection);
                    ImGui::EndDisabled();
                    int reflectionSamples = int(hybrid.reflectionSamples);
                    if (ImGui::SliderInt("Reflection rays/pixel", &reflectionSamples, 1, 16))
                        hybrid.reflectionSamples = uint32_t(reflectionSamples);
                    ImGui::BeginDisabled(!hybrid.shadows);
                    ImGui::Checkbox("Shadow temporal reconstruction", &hybrid.shadowTemporalReconstruction);
                    ImGui::BeginDisabled(!hybrid.shadowTemporalReconstruction);
                    ImGui::Checkbox("Shadow SVGF", &hybrid.shadowSVGF);
                    ImGui::EndDisabled();
                    ImGui::EndDisabled();
                    ImGui::Checkbox("Hybrid temporal reconstruction", &hybrid.temporalReconstruction);
                    ImGui::SliderFloat("Hybrid history weight", &hybrid.historyWeight, 0, .98f);
                    int view = int(hybrid.view);
                    if (ImGui::Combo("Hybrid view", &view, "Final\0Shadows\0Ambient occlusion\0Reflections\0"))
                        hybrid.view = HybridView(view);
                    if (hybrid.view == HybridView::Shadows)
                    {
                        int light = int(hybrid.shadowLight);
                        if (ImGui::SliderInt("Visibility light", &light, 0, 15))
                            hybrid.shadowLight = uint32_t(light);
                    }
                    ImGui::TextDisabled("GPU shader BVH; raster primary visibility");
                }
                if (mode == RenderMode::RayTracing)
                {
                    ImGui::TextUnformatted("Integrator");
                    ImGui::SetNextItemWidth(-1);
                    int integrator = static_cast<int>(path.integrator);
                    if (ImGui::Combo(
                          "##PathIntegrator",
                          &integrator,
                          capabilities.metropolis ? "Path tracing\0Bidirectional\0Photon mapping\0MLT\0"
                                                  : "Path tracing\0Bidirectional\0Photon mapping\0"))
                        path.integrator = static_cast<PathIntegrator>(integrator);
                    int spp = int(path.samplesPerFrame), bounces = int(path.maxBounces);
                    ImGui::TextUnformatted("Samples per frame");
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::SliderInt("##SamplesPerFrame", &spp, 1, 4))
                        path.samplesPerFrame = uint32_t(spp);
                    ImGui::TextUnformatted("Max bounces");
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::SliderInt("##MaxBounces", &bounces, 1, 8))
                        path.maxBounces = uint32_t(bounces);
                    ImGui::TextUnformatted("Render scale");
                    ImGui::SetNextItemWidth(-1);
                    ImGui::SliderFloat(
                      "##RenderScale", &path.resolutionScale, .25f, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                    if (path.integrator == PathIntegrator::PhotonMapping)
                    {
                        int photons = int(path.photonsPerIteration);
                        ImGui::TextUnformatted("Photons per iteration");
                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::SliderInt("##Photons", &photons, 256, 65536))
                            path.photonsPerIteration = uint32_t(photons);
                        ImGui::TextUnformatted("Initial gather radius");
                        ImGui::SetNextItemWidth(-1);
                        ImGui::SliderFloat("##PhotonRadius", &path.photonRadius, .03f, .5f, "%.3f");
                    }
                    if (path.integrator == PathIntegrator::Metropolis)
                    {
                        int bootstrap = int(path.mltBootstrapSamples);
                        ImGui::TextUnformatted("MLT bootstrap samples");
                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::SliderInt("##MLTBootstrap", &bootstrap, 256, 1048576))
                            path.mltBootstrapSamples = uint32_t(bootstrap);
                        ImGui::TextUnformatted("MLT large-step probability");
                        ImGui::SetNextItemWidth(-1);
                        ImGui::SliderFloat("##MLTLarge", &path.mltLargeStepProbability, .05f, 1, "%.2f");
                        ImGui::TextUnformatted("MLT mutation scale");
                        ImGui::SetNextItemWidth(-1);
                        ImGui::SliderFloat(
                          "##MLTScale", &path.mltMutationScale, .0001f, .25f, "%.4f", ImGuiSliderFlags_Logarithmic);
                    }
                    ImGui::Checkbox("Denoise", &path.denoise);
                    if (path.integrator != PathIntegrator::PathTracing)
                        ImGui::TextUnformatted(
                          path.integrator == PathIntegrator::PhotonMapping ? "Photon density estimate"
                                                                           : "Path-space MIS");
                    else
                        ImGui::Checkbox("Light sampling / MIS", &path.lightSampling);
                    ImGui::Text(
                      "%s: %u", path.integrator == PathIntegrator::Metropolis ? "Mutations/pixel" : "Samples", samples);
                    if (capabilities.softwareRayTracing)
                        ImGui::TextUnformatted("CPU ray tracing");
                    if (ImGui::Button("Reset accumulation"))
                        assets.requestedReset = true;
                }
            }
            if (!clips.empty() && ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::TextUnformatted("Clip");
                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo("##AnimationClip", clips.at(animation.clip).name.c_str()))
                {
                    for (size_t i = 0; i < clips.size(); ++i)
                        if (ImGui::Selectable(clips[i].name.c_str(), animation.clip == i))
                        {
                            animation.clip = uint32_t(i);
                            animation.time = 0;
                        }
                    ImGui::EndCombo();
                }
                ImGui::Checkbox("Playing", &animation.playing);
                ImGui::Checkbox("Loop", &animation.loop);
                ImGui::TextUnformatted("Speed");
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderFloat("##AnimationSpeed", &animation.speed, .1f, 4, "%.2f");
                ImGui::TextUnformatted("Time");
                ImGui::SetNextItemWidth(-1);
                ImGui::SliderFloat("##AnimationTime", &animation.time, 0, clips.at(animation.clip).duration, "%.2f s");
            }
            chooseAsset("Model", assets.config.models, assets.modelPath, assets.requestedModel);
            if (!assets.error.empty())
                ImGui::TextWrapped("%s", assets.error.c_str());
            if (ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen))
            {
                int background = clear[0] > 0.5f ? 1 : 0;
                ImGui::TextUnformatted("Background");
                ImGui::SetNextItemWidth(-1);
                ImGui::BeginDisabled((hasEnvironment && environment.showBackground));
                if (ImGui::Combo("##Background", &background, "Dark\0Light\0"))
                    clear = background ? std::array<float, 4>{0.8f, 0.85f, 0.95f, 1}
                                       : std::array<float, 4>{0.025f, 0.035f, 0.055f, 1};
                ImGui::EndDisabled();
            }
            auto slider = [](const char* label, float& value, float min, float max, const char* format) {
                ImGui::TextUnformatted(label);
                ImGui::SetNextItemWidth(-1);
                ImGui::PushID(label);
                const bool changed =
                  ImGui::SliderFloat("##value", &value, min, max, format, ImGuiSliderFlags_AlwaysClamp);
                ImGui::PopID();
                return changed;
            };
            if (model && ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
            {
                slider("Yaw", camera.yaw, -180, 180, "%.1f");
                slider("Pitch", camera.pitch, -85, 85, "%.1f");
                slider("Distance", camera.distance, 1.5f, 100, "%.2f");
                if (lighting == LightingMode::PBR || hasEnvironment || mode != RenderMode::Raster)
                    slider("Exposure", camera.exposure, 0.1f, 16, "%.1f");
                if (ImGui::Button("Reset camera"))
                    camera = initialCamera;
            }
            if (hasEnvironment && ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen))
            {
                chooseAsset("Map", assets.config.environments, assets.environmentPath, assets.requestedEnvironment);
                ImGui::Checkbox("Skybox", &environment.showBackground);
                ImGui::PushID("Environment");
                slider("Rotation", environment.rotation, -180, 180, "%.1f");
                slider("Intensity", environment.intensity, 0, 16, "%.2f");
                slider("Sky brightness", environment.backgroundIntensity, 0, 16, "%.2f");
                if (ImGui::Button("Reset environment"))
                    environment = initialEnvironment;
                ImGui::PopID();
            }
            if (
              model && (lighting == LightingMode::PBR || mode != RenderMode::Raster)
              && ImGui::CollapsingHeader("Main light", ImGuiTreeNodeFlags_DefaultOpen))
            {
                constexpr float degrees = 57.2957795131f;
                float           azimuth = std::atan2(light.direction[0], light.direction[2]) * degrees;
                float           elevation =
                  std::atan2(light.direction[1], std::hypot(light.direction[0], light.direction[2])) * degrees;
                const bool a = slider("Azimuth", azimuth, -180, 180, "%.1f"),
                           b = slider("Elevation", elevation, -90, 90, "%.1f");
                if (a || b)
                {
                    azimuth /= degrees;
                    elevation /= degrees;
                    light.direction = {
                      std::cos(elevation) * std::sin(azimuth),
                      std::sin(elevation),
                      std::cos(elevation) * std::cos(azimuth)};
                }
                if (ImGui::BeginTable("RGB", 3, ImGuiTableFlags_SizingStretchSame))
                {
                    for (int i = 0; i < 3; ++i)
                    {
                        ImGui::TableNextColumn();
                        slider(i == 0 ? "R" : i == 1 ? "G" : "B", light.color[i], 0, 1, "%.2f");
                    }
                    ImGui::EndTable();
                }
                slider("Intensity", light.intensity, 0, 100, "%.1f");
                if (ImGui::Button("Reset light"))
                    light = initialLight;
            }
        }
        ImGui::End();
        ImGui::Render();
    }
} // namespace render::detail
#endif
