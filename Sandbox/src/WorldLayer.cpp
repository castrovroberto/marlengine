#include "WorldLayer.h"
#include <imgui.h>
#include <fstream>
#include <sstream>

static constexpr const char* WorldFile = "Assets/Scenes/world.json";
static constexpr int StepsPerSecond = 60;   // the Application's fixed step is 1/60 s

WorldLayer::WorldLayer(Marle::Application& app)
    : Layer("World"), m_App(app)
{
    Marle::RegisterBuiltinComponents(m_Registry);
}

void WorldLayer::LoadWorld()
{
    // The first load keeps whatever loaded, problems and all; later changes to the file are only
    // applied when they are fully valid (see PollScene)
    m_Reloader = std::make_unique<Marle::SceneReloader>(WorldFile, m_Registry);
    Marle::SceneLoadResult result = m_Reloader->Load();
    for (const Marle::SceneProblem& problem : result.Problems)
        MRL_ERROR("{}", problem.ToString());
    m_Scene = result.Scene ? std::move(result.Scene) : std::make_unique<Marle::Scene>("Empty");

    m_Baseline = Marle::LoadScene(WorldFile, m_Registry).Scene;
    if (!m_Baseline) m_Baseline = std::make_unique<Marle::Scene>("Empty");
}

void WorldLayer::PollScene(bool force, bool carry, const char* what)
{
    Marle::SceneReload reload = m_Reloader->Poll(force);
    if (!reload.Attempted) return;

    if (!reload.Scene) {
        for (const Marle::SceneProblem& problem : m_Reloader->GetStatus().Problems)
            MRL_ERROR("{}", problem.ToString());
        MRL_ERROR("{} has {}; keeping the scene that is running", WorldFile, m_Reloader->GetStatus().Problems.size() == 1 ? "a problem" : std::to_string(m_Reloader->GetStatus().Problems.size()) + " problems");
        return;
    }

    // What the simulation had worked out (where the player is, how far the camera has turned and zoomed,
    // where the orbit has got to) stays, except for anything the file changed
    if (carry)
        Marle::CarryOverState(m_Registry, *m_Baseline, *m_Scene, *reload.Scene);
    m_Scene = std::move(reload.Scene);

    // A recording or a replay follows from the file it was made on, started fresh: a reload that carries state
    // over, or of a file that has changed, ends it. (A restart is a fresh load of the same file, and is fine.)
    if (m_Recorder.IsRecording() && (carry || m_Reloader->GetSceneHash() != m_Recorder.GetSceneHash())) {
        m_Recorder.Cancel();
        Note(false, "Recording stopped and dropped: the scene was reloaded, so the run no longer follows from the file it started on");
    }
    if (m_Replay && (carry || m_Reloader->GetSceneHash() != m_Replay->GetRecording().SceneHash)) {
        m_Replay.reset();
        Note(false, "Replay stopped: the scene was reloaded, so it can no longer reproduce the run");
        if (m_ExitWhenDone) m_App.Close();
    }

    m_Baseline = Marle::LoadScene(WorldFile, m_Registry).Scene;   // the file as it is now, never run
    if (!m_Baseline) m_Baseline = std::make_unique<Marle::Scene>("Empty");
    MRL_INFO("{} {} ({} entities)", what, WorldFile, m_Scene->GetEntityCount());
}

void WorldLayer::Note(bool good, const std::string& text)
{
    m_Notice = text;
    m_NoticeGood = good;
    if (good) MRL_INFO("{}", text); else MRL_ERROR("{}", text);
}

void WorldLayer::StartRecording(const std::string& file, bool restartFirst)
{
    if (m_Recorder.IsRecording()) return;
    if (m_Replay && restartFirst) {
        Note(false, "Cannot start a recording while a replay is playing");
        return;
    }
    m_RecordFile = file;

    // A recording starts from the scene as the file has it, so the world starts over first
    if (restartFirst) {
        RestartWorld();
        if (m_Reloader->GetStatus().KeptPrevious) {
            Note(false, "Cannot record: the scene file has problems, so it could not be restarted from the file");
            return;
        }
    }
    m_Recorder.Start(m_Scene->GetName(), m_Reloader->GetSceneHash(), m_Scene->GetSeed(), StepsPerSecond);
    Note(true, "Recording to " + m_RecordFile + (restartFirst ? " (the scene was restarted)" : ""));
}

void WorldLayer::StopRecording(const char* why)
{
    if (!m_Recorder.IsRecording()) return;

    const uint64_t checksum = Marle::Simulation::Checksum(m_Registry, *m_Scene);
    Marle::InputRecording recording = m_Recorder.Finish(checksum);
    Marle::RecordingWriteResult written = Marle::WriteRecording(recording);
    if (!written.Ok()) {
        for (const std::string& problem : written.Problems) MRL_ERROR("{}", problem);
        Note(false, "The recording could not be written");
        return;
    }

    std::string error;
    if (!Marle::WriteTextFile(m_RecordFile, written.Text, &error))
        Note(false, "The recording could not be saved: " + error);
    else
        Note(true, "Saved " + m_RecordFile + " (" + std::to_string(recording.GetStepCount()) + " steps, checksum " + Marle::Simulation::ChecksumText(checksum) + "; " + why + ")");
}

void WorldLayer::StartReplay(const std::string& file, bool exitWhenDone)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        Note(false, "Replay refused: cannot open " + file);
        return;
    }
    std::stringstream text;
    text << in.rdbuf();

    Marle::RecordingReadResult read = Marle::ReadRecording(text.str());
    if (!read.Ok()) {
        for (const std::string& problem : read.Problems) MRL_ERROR("{}: {}", file, problem);
        Note(false, "Replay refused: " + file + " has " + std::to_string(read.Problems.size()) + " problem(s)");
        return;
    }
    std::string mismatch = Marle::RecordingMismatch(read.Recording, m_Scene->GetName(), m_Reloader->GetSceneHash(), m_Scene->GetSeed(), StepsPerSecond);
    if (!mismatch.empty()) {
        Note(false, "Replay refused: " + mismatch);
        return;
    }

    m_Replay = std::make_unique<Marle::InputReplay>(std::move(read.Recording));
    m_ExitWhenDone = exitWhenDone;
    Note(true, "Replaying " + file + " (" + std::to_string(m_Replay->GetRecording().GetStepCount()) + " steps)");
}

void WorldLayer::FinishReplay()
{
    const Marle::InputRecording& recording = m_Replay->GetRecording();
    const uint64_t checksum = Marle::Simulation::Checksum(m_Registry, *m_Scene);
    if (!recording.HasChecksum)
        Note(true, "Replay finished (the recording has no checksum to compare with; this run ended at " + Marle::Simulation::ChecksumText(checksum) + ")");
    else if (checksum == recording.Checksum)
        Note(true, "Replay finished: the checksum matches (" + Marle::Simulation::ChecksumText(checksum) + "), the run was reproduced exactly");
    else
        Note(false, "Replay finished: the checksum does NOT match (recorded " + Marle::Simulation::ChecksumText(recording.Checksum) + ", this run " + Marle::Simulation::ChecksumText(checksum) + ")");

    StopRecording("the replay ended");
    m_Replay.reset();
    if (m_ExitWhenDone) m_App.Close();
}

void WorldLayer::RestartWorld()
{
    m_Scene->ClearRestartRequest();   // so a refused restart (a file with problems) is not asked again every step
    PollScene(true, false, "Restarted");
}

float WorldLayer::GetZoom() const
{
    const Marle::Camera* camera = m_Scene->FindEntity("camera").TryGet<Marle::Camera>();
    return camera ? camera->Zoom : 1.0f;
}

float WorldLayer::GetCameraRotation() const
{
    const Marle::Transform* camera = m_Scene->FindEntity("camera").TryGet<Marle::Transform>();
    return camera ? camera->Rotation : 0.0f;
}

glm::vec2 WorldLayer::GetPlayerPosition() const
{
    const Marle::Transform* player = m_Scene->FindEntity("player").TryGet<Marle::Transform>();
    return player ? player->Position : glm::vec2(0.0f);
}

void WorldLayer::OnAttach()
{
    m_Renderer = std::make_unique<Marle::SceneRenderer>();
    LoadWorld();
}

void WorldLayer::OnDetach()
{
    // The renderer holds the scene's textures, and the framebuffer a GL object: release them while
    // the context is still alive.
    StopRecording("the game was closed");
    m_Viewport.reset();
    m_Renderer.reset();
    m_Reloader.reset();
}

void WorldLayer::OnUpdate(double dt)
{
    if (--m_ReloadCountdown <= 0) {   // about six times a second
        m_ReloadCountdown = 10;
        PollScene();
    }

    if (m_Recorder.IsRecording())
        m_Recorder.Record(m_App.GetInputFrame());

    // Everything that moves is a behavior in the scene, run with this step's input
    Marle::Simulation::Step(m_Registry, *m_Scene, m_App.GetInputFrame(), dt);

    if (m_Scene->IsRestartRequested())
        RestartWorld();

    if (m_Replay && m_Replay->IsFinished())
        FinishReplay();
}

void WorldLayer::OnRender(double alpha)
{
    // Render into the viewport panel's framebuffer when the panel was on screen last frame
    bool toPanel = m_PanelVisible;
    m_PanelVisible = false;   // OnImGuiRender sets it again if the panel is still on screen
    glm::vec2 target((float)m_App.GetWidth(), (float)m_App.GetHeight());
    if (toPanel) {
        target = m_PanelSize;
        uint32_t w = (uint32_t)target.x, h = (uint32_t)target.y;
        if (!m_Viewport)
            m_Viewport = std::make_unique<Marle::OpenGLFramebuffer>(w, h);
        else if (m_Viewport->GetWidth() != w || m_Viewport->GetHeight() != h)
            m_Viewport->Resize(w, h);
        toPanel = m_Viewport->IsComplete();
        if (toPanel) {
            m_Viewport->Bind();
            glClear(GL_COLOR_BUFFER_BIT);
        }
    }

    // Drawn between the previous and the current fixed step, so motion is smooth whatever the frame rate
    m_Renderer->Render(*m_Scene, target.x, target.y, (float)alpha);

    if (toPanel)
        m_Viewport->Unbind();
}

void WorldLayer::DrawScenePanel()
{
    if (!m_ShowScenePanel) return;
    const Marle::SceneReloadStatus& status = m_Reloader->GetStatus();

    ImGui::SetNextWindowPos(ImVec2(10.0f, 245.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Scene", &m_ShowScenePanel, ImGuiWindowFlags_AlwaysAutoResize)) {
        const float width = ImGui::GetFontSize() * 24.0f;   // the panel is this wide; text wraps inside it
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);

        ImGui::TextUnformatted(status.File.c_str());
        ImGui::Text("%zu entities, loaded %u time%s", m_Scene->GetEntityCount(), status.Loads, status.Loads == 1 ? "" : "s");

        auto coloredText = [](const ImVec4& color, const char* text) {
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextUnformatted(text);
            ImGui::PopStyleColor();
        };
        if (status.Missing)
            coloredText(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "File is missing; keeping the scene that is running");
        else if (status.KeptPrevious)
            coloredText(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Reload refused: keeping the scene that is running");
        else if (!status.LastOk)
            coloredText(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Loaded with problems");
        else
            coloredText(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "OK");

        if (status.Failures > 0) ImGui::TextDisabled("%u reload%s refused so far", status.Failures, status.Failures == 1 ? "" : "s");
        if (ImGui::Button("Reload now")) PollScene(true);

        if (m_Recorder.IsRecording())
            coloredText(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), ("Recording: " + std::to_string(m_Recorder.GetStepCount()) + " steps (F5 stops)").c_str());
        if (m_Replay)
            coloredText(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), ("Replaying: " + std::to_string(m_Replay->GetStep()) + " / " + std::to_string(m_Replay->GetRecording().GetStepCount()) + " steps").c_str());
        if (!m_Notice.empty())
            coloredText(m_NoticeGood ? ImVec4(0.7f, 0.9f, 0.7f, 1.0f) : ImVec4(1.0f, 0.6f, 0.4f, 1.0f), m_Notice.c_str());

        if (!status.Problems.empty()) {
            ImGui::SeparatorText("Problems");
            ImGui::BeginChild("problems", ImVec2(width, 190.0f), ImGuiChildFlags_Borders);
            ImGui::PushTextWrapPos(width - ImGui::GetStyle().ScrollbarSize);
            for (const Marle::SceneProblem& problem : status.Problems) {
                ImGui::Text("%s%s%s", problem.Path.c_str(), problem.Path.empty() ? "" : ": ", problem.Message.c_str());
                ImGui::Separator();
            }
            ImGui::PopTextWrapPos();
            ImGui::EndChild();
        }
        ImGui::TextDisabled("F3 toggles this panel, F5 records");
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
}

void WorldLayer::OnImGuiRender()
{
    DrawScenePanel();

    if (!m_ShowPanel) return;

    ImGui::SetNextWindowPos(ImVec2(340.0f, 40.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(900.0f, 540.0f), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    bool open = ImGui::Begin("Viewport", &m_ShowPanel, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    if (open) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        m_PanelSize = { glm::max(avail.x, 1.0f), glm::max(avail.y, 1.0f) };
        m_PanelVisible = true;

        if (m_Viewport && m_Viewport->IsComplete()) {
            // Framebuffer rows run bottom to top, so flip v
            ImGui::Image((ImTextureID)(intptr_t)m_Viewport->GetColorAttachment(), avail, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
            // Let the world see the mouse wheel (zoom) while the pointer is over the picture
            if (ImGui::IsItemHovered())
                ImGui::SetNextFrameWantCaptureMouse(false);
        }
    }
    ImGui::End();
}

void WorldLayer::OnEvent(Marle::Event& e)
{
    if (e.GetEventType() != Marle::EventType::KeyPressed) return;

    auto& key = static_cast<Marle::KeyPressedEvent&>(e);
    if (key.GetKeyCode() == Marle::Key::F2 && key.GetRepeatCount() == 0) {
        m_ShowPanel = !m_ShowPanel;
        e.SetHandled();
    }
    else if (key.GetKeyCode() == Marle::Key::F3 && key.GetRepeatCount() == 0) {
        m_ShowScenePanel = !m_ShowScenePanel;
        e.SetHandled();
    }
    else if (key.GetKeyCode() == Marle::Key::F5 && key.GetRepeatCount() == 0) {
        if (m_Recorder.IsRecording()) StopRecording("stopped");
        else StartRecording(m_RecordFile, true);
        e.SetHandled();
    }
}
