#include "core_dll/hook/HookBatch.hpp"
#include "core_dll/mbaa_mem/BossCharacterSelect.hpp"
#include "core_dll/mbaa_mem/GameBuildGuard.hpp"
#include "core_dll/mbaa_mem/MbaaAddresses.hpp"
#include "core_dll/common/DebugLog.hpp"
#include "shared_contracts/BossCharacters.hpp"
#include "core_dll/ui/HudResources.hpp"
#include "core_dll/mbaa_mem/SteamV158StartupSignatures.hpp"
#include "shared_contracts/ProcessMemory.hpp"
#include <MinHook.h>
#include <vector>
using cccaster::game_memory::GameRuntime;
#include <atomic>
#include <cstring>
#include <string>
#include <cstdio>

extern "C" {
void* cc_boss_palette_original=nullptr;
void* cc_boss_scale_original=nullptr;
void* cc_boss_scale_resume=nullptr;
void* cc_boss_file_original=nullptr;
unsigned __cdecl cc_boss_preview_scale(unsigned*);
unsigned __cdecl cc_boss_preview_id(unsigned);
int __cdecl cc_boss_file_load(const char*,void*,unsigned,unsigned);
// Steam519DB0: ECX=path,EDX=out,stack=flags/wait、RET（callerが8byteを破棄）。
__attribute__((naked)) void cc_boss_file_hook() {
    __asm__ __volatile__("pushl 8(%esp); pushl 8(%esp); pushl %edx; pushl %ecx;"
        "call _cc_boss_file_load; addl $16,%esp; ret");
}
__attribute__((naked)) int __cdecl cc_boss_file_call(const char*,void*,unsigned,unsigned) {
    __asm__ __volatile__("pushl 16(%esp); pushl 16(%esp); movl 12(%esp),%ecx; movl 16(%esp),%edx;"
        "call *_cc_boss_file_original; addl $8,%esp; ret");
}
// Steam4DFEF0: ECX=resource,EDX=character,stack=component、RET（引数はcaller破棄）。
__attribute__((naked)) void cc_boss_palette_hook() {
    __asm__ __volatile__("pushfl; pushal; pushl %edx; call _cc_boss_preview_id; addl $4,%esp;"
                         "movl %eax,20(%esp); popal; popfl; jmp *_cc_boss_palette_original");
}
// Steamの倍率はx87でなくD3DXMatrixScalingの即値引数2つ。整数ビットで置換しx87を増やさない。
__attribute__((naked)) void cc_boss_scale_hook() {
    __asm__ __volatile__("pushfl; pushal; pushl %edi; call _cc_boss_preview_scale; addl $4,%esp;"
        "movl %eax,36(%esp); movl %eax,40(%esp); popal; popfl; jmp *_cc_boss_scale_resume");
}
}

namespace cccaster::boss::selection {
namespace {
bool installed=false,enabled=false;
std::atomic<bool> presentation{false};
using PreviewLoader=unsigned (__fastcall*)(void*,const char*);
PreviewLoader originalPreview=nullptr;
unsigned* Grid(){return *reinterpret_cast<unsigned**>(GameRuntime::Preferred(0x7d8ca4));}
unsigned* Cursor(unsigned side){return side ? CC_P2_SELECTOR_MODE_ADDR : CC_P1_SELECTOR_MODE_ADDR;}

// 元のファイルが見つからない表示画像だけを共用する。戦闘定義の名前は変更しない。
std::string ImageAlias(const char* path) {
    std::string value=path ? path : "";
    for(auto& ch:value){if(ch=='/')ch='\\';if(ch>='A' && ch<='Z')ch+=32;}
    struct Prefix{const char* text;bool giant;};
    constexpr Prefix prefixes[]={{".\\grp\\c_sel_aa\\chara\\csel_c",true},
        {".\\grp\\c_sel_aa\\palette\\color_c",true},{".\\grp\\gauge_aa\\face\\face",false},
        {".\\grp\\cut\\cut_",false},
        {".\\grp\\vsdemo_aa\\vs_cut\\vs_cut",true},
        {".\\grp\\vsdemo_aa\\vs_flash\\vs_fl",true},
        {".\\grp\\vsdemo_aa\\vs_color\\vs_chcolor",true},
        {".\\grp\\vsdemo_aa\\vs_name00\\vs_name00_",true},
        {".\\grp\\vsdemo_aa\\vs_name01\\vs_name01_",true},
        {".\\grp\\vsdemo_aa\\vs_command\\vs_com",true},
        {".\\grp\\vsdemo_aa\\vs_command\\jpn\\vs_com",true},
        {".\\grp\\vsdemo_aa\\vs_command\\eng\\vs_com",true}};
    for(const auto& prefix:prefixes) {
        const size_t n=std::strlen(prefix.text);
        if(value.compare(0,n,prefix.text) || value.size()<n+2 || value[n]<'0' || value[n]>'9' || value[n+1]<'0' || value[n+1]>'9')continue;
        const unsigned id=(value[n]-'0')*10+value[n+1]-'0';
        if(!IsBoss(id) || id==32 || (id==16 && !prefix.giant))return {};
        auto base=Base(id);
        // 巨大秋葉の固有技表は0のみ。ボスタッグの元キャラ35にも3の画像はない。
        if(value.find("\\vs_command\\")!=std::string::npos && (id==16 || id==85)) {
            if(id==16)base=16;
            if(value.size()>n+3 && value[n+2]=='_')value[n+3]='0';
        }
        value[n]=char('0'+base/10);value[n+1]=char('0'+base%10);
        return value;
    }
    return {};
}
int LoadFile(const char* path,void* out,unsigned flags,unsigned wait) {
    const auto result=cc_boss_file_call(path,out,flags,wait);
    if(result || !presentation.load(std::memory_order_relaxed))return result;
    const auto alias=ImageAlias(path);
    if(alias.empty())return result;
    const auto loaded=cc_boss_file_call(alias.c_str(),out,flags,wait);
    domain::session::DebugLog("[BossSelect] IMAGE from=%s to=%s loaded=%d",path,alias.c_str(),loaded);
    return loaded;
}
// Steamではpreview生成が2箇所へインライン化された。実資源ポインタを照合し、
// 同じ定義ローダーのうちキャラ選択の2componentだけを代替する。
__attribute__((force_align_arg_pointer)) unsigned __fastcall LoadPreview(void* object,const char* path) {
    if(presentation.load(std::memory_order_relaxed)) {
        const auto root=*reinterpret_cast<const uintptr_t*>(GameRuntime::Preferred(0x7b433c));
        const auto table=*reinterpret_cast<const uintptr_t*>(GameRuntime::Preferred(0x5c5144));
        if(root && table)for(unsigned side=0;side<2;++side) {
            const auto preview=root+side*0x1dc;
            const auto id=*reinterpret_cast<const unsigned*>(preview+4);
            if(!IsBoss(id))continue;
            for(unsigned part=0;part<2;++part) {
                const auto resource=preview+0x1a8+part*12;
                if(*reinterpret_cast<const uintptr_t*>(resource+8)!=reinterpret_cast<uintptr_t>(object))continue;
                if(id==32)return originalPreview(object,".\\data\\hermes_0.txt");
                const auto base=Base(id);
                if(base>=*reinterpret_cast<const unsigned*>(table+12))break;
                const auto data=*reinterpret_cast<const uintptr_t*>(table+4);
                const auto entry=*reinterpret_cast<const unsigned*>(table)==0
                    ? data+base*(*reinterpret_cast<const unsigned*>(table+8))
                    : reinterpret_cast<const uintptr_t*>(data)[base];
                const auto* name=reinterpret_cast<const char*>(entry+0x34+part*32);
                char alias[96];
                std::snprintf(alias,sizeof(alias),".\\data\\_csel\\%.*s.txt",32,name);
                return originalPreview(object,alias);
            }
        }
    }
    return originalPreview(object,path);
}
template<class T> bool Hook(uintptr_t address,void* hook,T* original) {
    auto* site=reinterpret_cast<void*>(GameRuntime::Preferred(address));
    return MH_CreateHook(site,hook,reinterpret_cast<void**>(original))==MH_OK &&
        cccaster::hook_batch::Enable(site)==MH_OK;
}
bool PatchAtlas() {
    std::vector<std::array<uint8_t,4>> before,after;
    std::vector<uintptr_t> addresses;
    for(const auto table:{0x5897e8u,0x589980u})for(const auto id:Characters) {
        if(id<50 && !(table==0x589980 && id==16))continue;
        const auto address=GameRuntime::Preferred(table+id*4,4);
        std::array<uint8_t,4> old{},replacement{};
        std::memcpy(old.data(),reinterpret_cast<const void*>(address),4);
        std::memcpy(replacement.data(),reinterpret_cast<const void*>(GameRuntime::Preferred(table+Base(id)*4,4)),4);
        before.push_back(old);after.push_back(replacement);addresses.push_back(address);
    }
    std::vector<patch::Spec> specs;
    for(size_t i=0;i<addresses.size();++i)specs.push_back({"boss_select_atlas",addresses[i],before[i],after[i]});
    const auto result=patch::Apply(specs);
    if(result.rollbackFailed)ExitProcess(ERROR_WRITE_FAULT);
    return bool(result);
}

bool Install() {
    if(installed)return true;
    if(!game_build::RuntimeValidated())return false;
    for(const auto& signature:game_memory::steam_v158_startup::BossSelection)
        if(!game_memory::steam_code::Matches(signature))return false;
    const auto init=MH_Initialize();
    if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED)return false;
    cc_boss_scale_resume=reinterpret_cast<void*>(GameRuntime::Preferred(0x4e0037));
    if(!Hook(0x519db0,reinterpret_cast<void*>(cc_boss_file_hook),&cc_boss_file_original) ||
       !Hook(0x49f920,reinterpret_cast<void*>(LoadPreview),&originalPreview) ||
       !Hook(0x4dfef0,reinterpret_cast<void*>(cc_boss_palette_hook),&cc_boss_palette_original) ||
       !Hook(0x4e0028,reinterpret_cast<void*>(cc_boss_scale_hook),&cc_boss_scale_original) ||
       !PatchAtlas())return false;
    installed=true;
    domain::session::DebugLog("[BossSelect] INSTALLED nativeGrid=63 randomCell=49 sides=4+4");
    return true;
}
}
bool Enabled(){return enabled;}
void DrawLabels() {
    if(!enabled || *CC_GAME_MODE_ADDR!=CC_GAME_MODE_CHARA_SELECT)return;
    const auto* grid=Grid();
    if(!grid)return;
    const auto viewport=hud::CurrentViewport();
    const float sx=viewport.width/640.f,sy=viewport.height/480.f;
    const float size=8.5f*(std::min)(sx,sy);
    auto* font=hud::Font(3,(std::min)(sx,sy));
    auto* draw=ImGui::GetForegroundDrawList();
    for(const auto cell:Cells) {
        const auto* entry=grid+cell*6;
        if(!IsBoss(entry[2]) || *reinterpret_cast<const float*>(entry+5)>.01f)continue;
        const auto width=font->CalcTextSizeA(size,10000,0,"[BOSS]").x;
        const ImVec2 at{viewport.x+(entry[3]+24)*sx-width/2,viewport.y+(entry[4]+8)*sy};
        draw->AddRectFilled({at.x-1.5f*sx,at.y},{at.x+width+1.5f*sx,at.y+size+sy},IM_COL32(24,18,0,215));
        draw->AddText(font,size,at,IM_COL32(255,226,35,255),"[BOSS]");
    }
}
bool Configure(unsigned mode,bool allow) {
    // 同一PCのVersusにはフック・セル・選択値を適用しない。
    if(mode==5)return true;
    if(mode!=0 && mode!=1 && mode!=2 && mode!=3)return true;
    if(!Install())return false;
    presentation.store(true,std::memory_order_relaxed);
    if(enabled!=allow){enabled=allow;domain::session::DebugLog("[BossSelect] ENABLED mode=%u value=%u",mode,unsigned(enabled));}
    if(*CC_GAME_MODE_ADDR!=CC_GAME_MODE_CHARA_SELECT)return true;
    if(auto* grid=Grid())for(unsigned i=0;i<Characters.size();++i)grid[Cells[i]*6+2]=enabled ? Characters[i] : UINT32_MAX;
    if(enabled)for(unsigned side=0;side<2;++side) {
        auto* cursor=Cursor(side);
        if(!IsBoss(cursor[4]))continue;
        // 標準の再入場は静的な通常キャラ表を検索するため、ボスのセルが-1になる。
        // 動的一覧を使う通常入力へ戻す前に、この列の対応セルを復元する。
        if(cursor[3]>=63)cursor[3]=Cell(cursor[4]);
        cursor[5]=Moon(cursor[4]);
        // 各ボスは収録スタイルが1つだけ。3択の標準ムーンを回して未収録TXTへ入らない。
        if(cursor[0]==1) {
            cursor[0]=2;
            domain::session::DebugLog("[BossSelect] STYLE side=%u character=%u moon=%u",side,cursor[4],cursor[5]);
        }
    }
    return true;
}
}

extern "C" __attribute__((force_align_arg_pointer)) unsigned cc_boss_preview_id(unsigned character) {
    return cccaster::boss::Base(character);
}
extern "C" __attribute__((force_align_arg_pointer)) unsigned cc_boss_preview_scale(unsigned* resource) {
    const auto base=*reinterpret_cast<const uintptr_t*>(GameRuntime::Preferred(0x7b433c));
    if(base)for(unsigned side=0;side<2;++side) {
        const auto preview=base+side*0x1dc;
        if(reinterpret_cast<uintptr_t>(resource)==preview+0x1a8 &&
           *reinterpret_cast<const unsigned*>(preview+4)==32)return 0x3e800000; // 2.0 * 0.125
    }
    return 0x40000000; // 元の2.0
}
extern "C" __attribute__((force_align_arg_pointer)) int __cdecl cc_boss_file_load(
        const char* path,void* out,unsigned flags,unsigned wait) {
    return cccaster::boss::selection::LoadFile(path,out,flags,wait);
}
