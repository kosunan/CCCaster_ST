#include "StartupFileRead.hpp"
#include "SteamV15Signatures.hpp"
using cccaster::game_memory::GameRuntime;
extern "C" { uintptr_t cc_startup_sync_caller=0, cc_startup_read_success=0, cc_startup_read_async=0; }
#include "core_dll/mbaa_mem/StartupPatch.hpp"
#include "core_dll/common/StartupTrace.hpp"
#include "shared_contracts/ProcessMemory.hpp"
#include <atomic>
#include <cstdlib>

namespace {
bool active = false, verify = false, inlineRead = false;
std::atomic<unsigned> reads{0};
uintptr_t Site=0, WorkerOperand=0;
constexpr std::array<uint8_t, 6> Original{0x8D,0x46,0x40,0x50,0x6A,0};
std::array<uint8_t, 6> replacement{};
uint32_t Worker=0;
uint32_t diagnosticWorker = 0;

uint64_t Hash(const void* data, size_t size) {
    uint64_t hash = 14695981039346656037ull;
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 1099511628211ull;
    return hash;
}
bool Signature() {
    if(!cccaster::game_memory::startup::MatchesMenuCode()) return false;
    for(const auto& signature : cccaster::game_memory::steam_v15::FileRead)
        if(!cccaster::game_memory::steam_code::Matches(signature)) return false;
    return true;
}
}

extern "C" {
__attribute__((force_align_arg_pointer)) DWORD __cdecl cc_startup_read(void* object) {
    // Steam 46B770..46B899もRET 4ではなくRET。cdeclで呼ぶ。
    const auto result = reinterpret_cast<DWORD (__cdecl*)(void*)>(Worker)(object);
    reads.fetch_add(1, std::memory_order_relaxed);
    if (verify) {
        const auto* fields = static_cast<const uint32_t*>(object);
        cccaster::domain::session::DebugLog(
            "[StartupReadData] offset=%u bytes=%u requested=%u result=%lu hash=%016llX",
            fields[4], fields[8], fields[9], static_cast<unsigned long>(result),
            static_cast<unsigned long long>(Hash(reinterpret_cast<void*>(fields[12]), fields[8])));
    }
    return result;
}
__attribute__((force_align_arg_pointer)) DWORD WINAPI cc_startup_read_thread(void* object) {
    return cc_startup_read(object);
}
// Steam 46B4D5。同期呼出しの戻り先は[EBP+4]、ESIがfile object。
__attribute__((naked)) void cc_startup_read_gate() {
    __asm__ __volatile__(
        "movl 4(%ebp),%eax\n\tcmpl _cc_startup_sync_caller,%eax\n\tjne 1f\n\t"
        "cmpl $0,0x3c(%esi)\n\tjne 1f\n\tpushl %esi\n\tcall _cc_startup_read\n\taddl $4,%esp\n\t"
        "movl $0,0x3c(%esi)\n\tmovl $0,0x40(%esi)\n\tjmp *_cc_startup_read_success\n\t"
        "1: leal 0x40(%esi),%eax\n\tpushl %eax\n\tpushl $0\n\tjmp *_cc_startup_read_async\n\t");
}
}

namespace cccaster::game_memory::startup_file_read {
bool Change(bool enable) {
    std::vector<patch::Spec> sites;
    if (inlineRead) sites.push_back({"startup_sync_read", Site,
        enable ? Original : replacement, enable ? replacement : Original});
    const auto original = std::span(reinterpret_cast<const uint8_t*>(&Worker), 4);
    const auto diagnostic = std::span(reinterpret_cast<const uint8_t*>(&diagnosticWorker), 4);
    if (verify) sites.push_back({"startup_read_verify", WorkerOperand,
        enable ? original : diagnostic, enable ? diagnostic : original});
    const auto result = patch::Apply(sites);
    if (!result) {
        domain::session::DebugLog("[StartupFileRead] FAILED site=%s error=%s rollbackFailed=%u",
            result.name, patch::Name(result.error), unsigned(result.rollbackFailed));
        if (result.rollbackFailed) ExitProcess(ERROR_WRITE_FAULT);
        return false;
    }
    active = enable;
    return true;
}
void Initialize(uint8_t mode) {
    if (mode > 1 || !diagnostics::startup::HasGate() || diagnostics::startup::Baseline()) return;
    inlineRead = std::getenv("CCCASTER_STARTUP_IO_BASELINE") == nullptr;
    verify = std::getenv("CCCASTER_STARTUP_IO_VERIFY") != nullptr;
    if (!inlineRead && !verify) return;
    if (!Signature()) {
        domain::session::DebugLog("[StartupFileRead] signature mismatch; native reader retained");
        return;
    }
    Site=GameRuntime::Preferred(0x46b4d5,6);
    WorkerOperand=GameRuntime::Preferred(0x46b4dd,4);
    Worker=uint32_t(GameRuntime::Preferred(0x46b770));
    cc_startup_sync_caller=GameRuntime::Preferred(0x46b514);
    cc_startup_read_success=GameRuntime::Preferred(0x46b4b1);
    cc_startup_read_async=GameRuntime::Preferred(0x46b4db);
    replacement = {0xE9,0,0,0,0,0x90};
    const auto relative = uint32_t(reinterpret_cast<uintptr_t>(&cc_startup_read_gate) - (Site + 5));
    std::memcpy(replacement.data() + 1, &relative, 4);
    diagnosticWorker = uint32_t(reinterpret_cast<uintptr_t>(&cc_startup_read_thread));
    domain::session::DebugLog("[StartupFileRead] enabled=%u inline=%u verify=%u",
        unsigned(Change(true)), unsigned(inlineRead), unsigned(verify));
}
void Restore() {
    if (!active) return;
    if (!Change(false)) ExitProcess(ERROR_WRITE_FAULT);
    domain::session::DebugLog("[StartupFileRead] restored=1 reads=%u inline=%u",
        reads.load(std::memory_order_relaxed), unsigned(inlineRead));
}
}
