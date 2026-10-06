#include "shared_contracts/TrainingStandby.hpp"
#include <iostream>
#include <stdexcept>

using namespace cccaster::training_standby;
void Check(bool condition) { if (!condition) throw std::runtime_error("training standby contract failed"); }
int main() {
    Channel gui, game, other;
    Check(gui.Create()); Check(game.Open(gui.name));
    Check(other.Open(gui.name + L"_other", true));
    Check(gui.Update([](State& s) {
        s.generation = 7; s.heartbeat = GetTickCount64(); s.expires = std::time(nullptr)+60;
        std::snprintf(s.request,sizeof(s.request),"request-one");
        std::snprintf(s.name,sizeof(s.name),"OPPONENT");
    }));
    State state{}; Check(game.Read(state)); Check(state.Live(GetTickCount64(),std::time(nullptr)));
    Check(!game.Reply(6,1)); Check(!game.Reply(7,9));
    Check(game.Reply(7,2)); Check(!game.Reply(7,1));
    Check(gui.Read(state)); Check(state.reply == 2);
    Check(other.Read(state)); Check(state.reply == 0 && !state.request[0]);
    Check(gui.Update([](State& s) { ++s.generation; s.reply=0; s.expires=std::time(nullptr)-1; }));
    Check(!game.Reply(8,1));
    Check(gui.Update([](State& s) { s.expires=std::time(nullptr)+60; s.heartbeat=GetTickCount64()-4000; }));
    Check(!game.Reply(8,1));
    Check(gui.Update([](State& s) { s.heartbeat=GetTickCount64(); s.stopGame=true; }));
    Check(!game.Reply(8,1)); Check(other.Read(state)); Check(!state.stopGame);
    gui.Close(); Check(game.Read(state)); Check(state.stopGame);
    std::cout << "training standby: stale/duplicate/expired replies, heartbeat, isolation OK\n";
}
