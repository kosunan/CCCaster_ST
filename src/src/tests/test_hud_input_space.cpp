#include "core_dll/ui/HudInputSpace.hpp"
#include "core_dll/ui/HudDrawSpace.hpp"
#include <cstdio>
#include <cfloat>
#include <cmath>
#include <vector>

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

    // HUDの新しい入力座標は表示先ピクセル。全画面の黒帯だけを差し引く。
    io.AddMousePosEvent(420, 120);
    frame({1920,1440}, {1920,1440}, {320,0});
    check(io.MousePos.x == 100 && io.MousePos.y == 120, "display-space pointer excludes black bars");
    frame({1920,1440}, {1920,1440}, {320,0});
    check(io.MousePos.x == 100 && io.MousePos.y == 120, "display-space pointer remains stationary");
    frame({800,600}, {800,600});
    check(io.MousePos.x == 420 && io.MousePos.y == 120, "window return uses actual client pixels");

    const auto near = [](float a, float b) { return std::abs(a-b) < .01f; };
    for (const auto buffer : {ImVec2(1920,1080), ImVec2(640,480)}) {
        for (const auto display : {ImVec2(640,480), ImVec2(800,600), ImVec2(1280,720),
                                   ImVec2(1920,1080), ImVec2(1920,1440)}) {
            const auto bounds = cccaster::hud::DisplayViewport({0,0,buffer.x,buffer.y},buffer,display);
            const auto layout = cccaster::hud::Layout::Fit(bounds);
            check(near(layout.X(640)-layout.X(0),640*layout.scale) &&
                  near(layout.Y(480)-layout.Y(0),480*layout.scale), "HUD layout keeps 4:3 in display pixels");
            io.DisplaySize = display;
            ImGui::NewFrame();
            auto* draw = ImGui::GetForegroundDrawList();
            draw->PushClipRect({90,90},{310,210});
            draw->AddRectFilled({100,100},{200,200},IM_COL32_WHITE);
            draw->AddRectFilled({210,100},{310,150},IM_COL32_WHITE); // 2:1のエンブレム相当
            draw->AddText({100,170},IM_COL32_WHITE,"HUD 0123");
            draw->PopClipRect();
            ImGui::Render();
            auto* data = ImGui::GetDrawData();
            check(data->CmdListsCount == 1, "actual ImGui geometry generated");
            auto* list = data->CmdLists[0];
            std::vector<ImVec2> uv;
            for (auto& vertex : list->VtxBuffer) uv.push_back(vertex.uv);
            cccaster::hud::MapDrawDataToBuffer(*data,buffer);
            const auto physical = [&](int i) { const auto p = list->VtxBuffer[i].pos;
                return ImVec2(p.x*display.x/buffer.x,p.y*display.y/buffer.y); };
            const auto a = physical(0), b = physical(2), c = physical(4), d = physical(6);
            check(near(b.x-a.x,100) && near(b.y-a.y,100), "square stays square after final presentation");
            check(near(d.x-c.x,100) && near(d.y-c.y,50), "emblem keeps 2:1 after final presentation");
            const auto clip = list->CmdBuffer[0].ClipRect;
            check(near(clip.x*display.x/buffer.x,90) && near(clip.w*display.y/buffer.y,210),
                  "clip edges follow displayed geometry");
            for (int i=0; i<list->VtxBuffer.Size; ++i)
                check(list->VtxBuffer[i].uv.x == uv[i].x && list->VtxBuffer[i].uv.y == uv[i].y,
                      "font and image texture coordinates retained");
            cccaster::hud::MapDrawDataToBuffer(*data,buffer);
            check(near(physical(2).x,200) && near(physical(2).y,200), "repeated mapping cannot distort geometry");
        }
    }
    ImGui::DestroyContext();
    return failures ? 1 : 0;
}
