#include "test_support.hpp"
// UI の待機用時計をゲーム起動なしで検証する。
#include "core_dll/ui/OverlayRenderer.hpp"
#include <chrono>
#include <thread>

int main() {
    using cccaster::overlay::OverlayRenderer;
    CC_CASE("OverlayRenderer: ミリ秒時計が待機後も単調に進む");
    const auto before = OverlayRenderer::GetTimeMs();
    CC_CHECK(before > 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const auto after = OverlayRenderer::GetTimeMs();
    CC_CHECK(after >= before);
    CC_CHECK(after > before);
    return cccaster::test::Summarize("platform");
}
