#include "core_dll/ui/HudInputSpace.hpp"
#include <cstdio>
#include <cfloat>

int main() {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.DisplaySize = {640, 480};
    io.Fonts->AddFontDefault(); io.Fonts->Build();
    cccaster::hud::InputSpace input;
    int failures = 0;
    const auto check = [&](bool yes, const char* label) {
        if (!yes) { ++failures; std::fprintf(stderr, "FAIL %s\n", label); }
    };
    const auto frame = [&](ImVec2 client, ImVec2 buffer, ImVec2 origin = ImVec2(0, 0)) {
        io.DisplaySize = buffer;
        input.Begin(client, buffer, origin); ImGui::NewFrame(); input.End(); ImGui::EndFrame();
    };
    io.AddMousePosEvent(200, 100);
    io.AddMouseButtonEvent(0, true);
    io.AddMousePosEvent(400, 300);
    frame({1280, 960}, {640, 480});
    check(io.MousePos.x == 100 && io.MousePos.y == 50 && io.MouseDown[0], "scaled click position");
    check(ImGui::GetCurrentContext()->InputEventsQueue.Size > 0, "fast drag event carried to next frame");
    frame({1280, 960}, {640, 480});
    check(io.MousePos.x == 200 && io.MousePos.y == 150, "carried event scaled once");
    frame({1280, 960}, {640, 480});
    check(io.MousePos.x == 200 && io.MousePos.y == 150, "stationary pointer does not drift");
    frame({640, 480}, {1280, 960});
    check(io.MousePos.x == 800 && io.MousePos.y == 600, "backbuffer resolution change");
    check(io.MouseClickedPos[0].x == 400, "drag origin follows resolution change");
    frame({640, 480}, {640, 480});
    check(io.MousePos.x == 400 && io.MousePos.y == 300, "return to unit scale");
    io.AddMouseButtonEvent(0, false);
    frame({640, 480}, {640, 480});
    io.AddMousePosEvent(960, 540);
    io.AddMouseButtonEvent(0, true);
    io.AddMousePosEvent(600, 270);
    frame({1440, 1080}, {640, 480}, {240, 0});
    check(io.MousePos.x == 320 && io.MousePos.y == 240, "borderless center excludes black bars");
    frame({1440, 1080}, {640, 480}, {240, 0});
    check(io.MousePos.x == 160 && io.MousePos.y == 120, "borderless carried event transformed once");
    frame({1440, 1080}, {640, 480}, {240, 0});
    check(io.MousePos.x == 160 && io.MousePos.y == 120, "borderless stationary pointer does not drift");
    frame({640, 480}, {640, 480});
    check(io.MousePos.x == 600 && io.MousePos.y == 270, "window restoration removes black bar offset");
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    frame({1280, 960}, {640, 480});
    check(!ImGui::IsMousePosValid(), "outside-window sentinel retained");
    ImGui::DestroyContext();
    return failures ? 1 : 0;
}
