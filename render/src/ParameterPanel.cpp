#include "ParameterPanel.hpp"
#include "Color.hpp"
#ifdef NOVA_HAS_UI
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <imgui_impl_glfw.h>
#include <stdexcept>

namespace render::detail
{
    ParameterPanel::ParameterPanel(GLFWwindow* value)
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
        initialized = ImGui_ImplGlfw_InitForOther(window, true);
        if (!initialized)
        {
            ImGui::DestroyContext(context);
            context = nullptr;
            throw std::runtime_error("Initialize parameter UI input failed");
        }
    }
    ParameterPanel::~ParameterPanel()
    {
        if (context)
        {
            Activate();
            if (initialized)
                ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext(context);
        }
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
        glfwGetCursorPos(window, &x, &y);
        return ImGui::GetIO().WantCaptureMouse
               || (x >= position.x && y >= position.y && x < position.x + size.x && y < position.y + size.y);
    }
    bool ParameterPanel::WantsKeyboard() const
    {
        Activate();
        return ImGui::GetIO().WantCaptureKeyboard;
    }
    void ParameterPanel::Draw(
      bool                                     model,
      LightingMode                             lighting,
      OrbitCamera&                             camera,
      DirectionalLight&                        light,
      std::array<float, 4>&                    clear,
      bool                                     hasEnvironment,
      EnvironmentSettings&                     environment,
      PanelAssets&                             assets,
      RenderMode                               mode,
      const RendererCapabilities&              capabilities,
      PathTracingSettings&                     path,
      uint32_t                                 samples,
      const std::array<ComparisonProgress, 4>& comparison,
      const std::vector<AnimationClip>&        clips,
      AnimationState&                          animation)
    {
        Activate();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        const auto display = ImGui::GetIO().DisplaySize;
        if (mode == RenderMode::IntegratorComparison)
        {
            const bool  vertical = display.y > display.x;
            const float side     = vertical ? std::min(display.x, display.y / 4) : std::min(display.x / 4, display.y),
                        top      = (display.y - side * (vertical ? 4 : 1)) / 2,
                        left     = (display.x - side * (vertical ? 1 : 4)) / 2;
            const char* names[]  = {"PT", "BDPT", "Photon mapping", "MLT"};
            for (size_t i = 0; i < 4; ++i)
            {
                char label[128];
                std::snprintf(
                  label,
                  sizeof(label),
                  "%s | %u %s | %.0f ms%s",
                  names[i],
                  comparison[i].samples,
                  i == 3 ? "mpp" : "spp",
                  comparison[i].milliseconds,
                  comparison[i].complete ? " | paused" : "");
                auto*        draw = ImGui::GetBackgroundDrawList();
                const ImVec2 pos{
                  left + (vertical ? 0 : float(i) * side) + 5, std::max(0.f, top + (vertical ? float(i) * side : 0))};
                draw->AddRectFilled(pos, ImVec2{pos.x + side - 8, pos.y + 21}, IM_COL32(0, 0, 0, 210));
                draw->AddText(pos, IM_COL32(255, 255, 255, 255), label);
            }
        }
        const float width = std::min(250.0f, std::max(80.0f, display.x - 24));
        ImGui::SetNextWindowPos({std::max(0.0f, display.x - width - 12), 12}, ImGuiCond_Always);
        ImGui::SetNextWindowSizeConstraints({width, 0}, {width, std::max(40.0f, display.y - 24)});
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
                      mode == RenderMode::Raster                 ? "Raster"
                      : mode == RenderMode::IntegratorComparison ? "Integrator comparison"
                                                                 : "Ray tracing"))
                {
                    if (ImGui::Selectable("Raster", mode == RenderMode::Raster))
                        assets.requestedMode = RenderMode::Raster;
                    ImGui::BeginDisabled(!capabilities.pathTracing);
                    if (ImGui::Selectable("Ray tracing", mode == RenderMode::RayTracing))
                        assets.requestedMode = RenderMode::PathTracing;
                    if (ImGui::Selectable("Integrator comparison", mode == RenderMode::IntegratorComparison))
                        assets.requestedMode = RenderMode::IntegratorComparison;
                    if (!capabilities.pathTracing && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        ImGui::SetTooltip("%s", capabilities.reason.c_str());
                    ImGui::EndDisabled();
                    ImGui::EndCombo();
                }
                if (mode != RenderMode::Raster)
                {
                    const bool comparing = mode == RenderMode::IntegratorComparison;
                    if (comparing)
                    {
                        ImGui::TextWrapped(
                          "Emitter-only lighting; no denoising. PT | BDPT | Photon mapping | MLT. One camera sample "
                          "per "
                          "iteration.");
                        int budget = path.comparisonMilliseconds ? 1 : 0;
                        if (ImGui::Combo("Budget", &budget, "Fixed samples\0Fixed time\0"))
                            path.comparisonMilliseconds = budget ? 1000 : 0;
                        if (budget)
                        {
                            int ms = int(path.comparisonMilliseconds);
                            if (ImGui::SliderInt("Time / method (ms)", &ms, 1, 60000))
                                path.comparisonMilliseconds = uint32_t(ms);
                        }
                        else
                        {
                            int n = int(path.comparisonSamples);
                            if (ImGui::SliderInt("Samples / method", &n, 1, 65536))
                                path.comparisonSamples = uint32_t(n);
                        }
                    }
                    ImGui::BeginDisabled(comparing);
                    ImGui::TextUnformatted("Integrator");
                    ImGui::SetNextItemWidth(-1);
                    int integrator = static_cast<int>(path.integrator);
                    if (ImGui::Combo(
                          "##PathIntegrator", &integrator, "Path tracing\0Bidirectional\0Photon mapping\0MLT\0"))
                        path.integrator = static_cast<PathIntegrator>(integrator);
                    ImGui::EndDisabled();
                    int spp = int(path.samplesPerFrame), bounces = int(path.maxBounces);
                    ImGui::BeginDisabled(comparing);
                    ImGui::TextUnformatted("Samples per frame");
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::SliderInt("##SamplesPerFrame", &spp, 1, 4))
                        path.samplesPerFrame = uint32_t(spp);
                    ImGui::EndDisabled();
                    ImGui::TextUnformatted("Max bounces");
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::SliderInt("##MaxBounces", &bounces, 1, 8))
                        path.maxBounces = uint32_t(bounces);
                    ImGui::TextUnformatted("Render scale");
                    ImGui::SetNextItemWidth(-1);
                    ImGui::SliderFloat(
                      "##RenderScale", &path.resolutionScale, .25f, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                    if (comparing || path.integrator == PathIntegrator::PhotonMapping)
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
                    if (comparing || path.integrator == PathIntegrator::Metropolis)
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
                    ImGui::BeginDisabled(comparing);
                    ImGui::Checkbox("Denoise", &path.denoise);
                    ImGui::EndDisabled();
                    if (comparing)
                        ImGui::TextUnformatted("PT: MIS enabled; photon density estimate");
                    else if (path.integrator != PathIntegrator::PathTracing)
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
                ImGui::BeginDisabled(
                  mode == RenderMode::IntegratorComparison || (hasEnvironment && environment.showBackground));
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
            if (
              mode != RenderMode::IntegratorComparison && hasEnvironment
              && ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen))
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
              mode != RenderMode::IntegratorComparison && model
              && (lighting == LightingMode::PBR || mode != RenderMode::Raster)
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
