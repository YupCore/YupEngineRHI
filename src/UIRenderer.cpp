#include "UIRenderer.h"

UIRenderer::UIRenderer(DeviceManager* deviceManager, UIData& ui)
    : ImGui_Renderer(deviceManager)
    , m_ui(ui)
{
    m_CommandList = GetDevice()->createCommandList();

    ImGui::GetIO().IniFilename = nullptr;
}

void UIRenderer::buildUI(void)
{
    if (!(m_ui.ShowUI && m_ui.SceneLoadedStatus))
        return;

    const auto& io = ImGui::GetIO();

    int width, height;
    GetDeviceManager()->GetWindowDimensions(width, height);

    ImGui::SetNextWindowPos(ImVec2(10.f, 10.f), 0);
    ImGui::Begin("Settings", 0, ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::Text("Renderer: %s", GetDeviceManager()->GetRendererString());
    double frameTime = GetDeviceManager()->GetAverageFrameTimeSeconds();
    if (frameTime > 0.0)
        ImGui::Text("%.3f ms/frame (%.1f FPS)", frameTime * 1e3, 1.0 / frameTime);

    if (ImGui::Button("Reload Shaders"))
        m_ui.ShaderReloadRequested = true;

    ImGui::Checkbox("Enable Light Probe", &m_ui.EnableLightProbe);
    if (m_ui.EnableLightProbe && ImGui::CollapsingHeader("Light Probe"))
    {
        ImGui::DragFloat("Diffuse Scale", &m_ui.LightProbeDiffuseScale, 0.01f, 0.0f, 10.0f);
        ImGui::DragFloat("Specular Scale", &m_ui.LightProbeSpecularScale, 0.01f, 0.0f, 10.0f);
    }

    ImGui::SliderFloat("Ambient Intensity", &m_ui.AmbientIntensity, 0.f, 1.f);

    ImGui::Checkbox("Enable Procedural Sky", &m_ui.EnableProceduralSky);
    if (m_ui.EnableProceduralSky && ImGui::CollapsingHeader("Sky Parameters"))
    {
        ImGui::SliderFloat("Brightness", &m_ui.SkyParams.brightness, 0.f, 1.f);
        ImGui::SliderFloat("Glow Size", &m_ui.SkyParams.glowSize, 0.f, 90.f);
        ImGui::SliderFloat("Glow Sharpness", &m_ui.SkyParams.glowSharpness, 1.f, 10.f);
        ImGui::SliderFloat("Glow Intensity", &m_ui.SkyParams.glowIntensity, 0.f, 1.f);
        ImGui::SliderFloat("Horizon Size", &m_ui.SkyParams.horizonSize, 0.f, 90.f);
    }
    ImGui::Checkbox("Enable DefferedShading", &m_ui.UseDeferredShading);
    ImGui::Checkbox("Enable SSAO", &m_ui.EnableSsao);
    ImGui::Checkbox("Enable FXAA", &m_ui.EnableFXAA);
    ImGui::Checkbox("Enable Shadows", &m_ui.EnableShadows);
    ImGui::Checkbox("Enable Translucency", &m_ui.EnableTranslucency);

    ImGui::Separator();
    ImGui::Checkbox("Enable Bloom", &m_ui.EnableBloom);
    ImGui::DragFloat("Bloom Sigma", &m_ui.BloomSigma, 0.01f, 0.1f, 100.f);
    ImGui::DragFloat("Bloom Alpha", &m_ui.BloomAlpha, 0.01f, 0.01f, 1.0f);
    ImGui::Separator();

    if (!m_ui.lights.empty() && ImGui::CollapsingHeader("Lights"))
    {
        if (ImGui::BeginCombo("Select Light", m_SelectedLight ? m_SelectedLight->GetName().c_str() : "(None)"))
        {
            for (const auto& light : m_ui.lights)
            {
                bool selected = m_SelectedLight == light;
                ImGui::Selectable(light->GetName().c_str(), &selected);
                if (selected)
                {
                    m_SelectedLight = light;
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (m_SelectedLight)
        {
            app::LightEditor(*m_SelectedLight);
        }
    }

    ImGui::TextUnformatted("Render Light Probe: ");
    uint32_t probeIndex = 1;
    for (auto probe : m_ui.LightProbes)
    {
        ImGui::SameLine();
        if (ImGui::Button(probe->name.c_str()))
        {
            m_ui.m_RenderCallback(*probe);
        }
    }

    ImGui::Separator();
    ImGui::Checkbox("Display Shadow Map", &m_ui.DisplayShadowMap);

    ImGui::End();
}
