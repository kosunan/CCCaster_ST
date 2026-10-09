#include "core_dll/engine/ExtraColorNetwork.hpp"
#include "core_dll/engine/ExtraColorStore.hpp"
#include "core_dll/engine/ExtraColorPage.hpp"
#include "core_dll/engine/ExtraColorSample.hpp"
#include "core_dll/common/DataPaths.hpp"
#include "test_support.hpp"
#include <fstream>
#ifdef _WIN32
#include <windows.h>
#endif
using namespace cccaster::training_palette;
namespace net=cccaster::training_palette::network;
net::Chunk Chunk(const net::Blob& blob,unsigned index) {
    net::Chunk result;result.epoch=blob.epoch;result.serial=blob.serial;result.hash=blob.hash;
    result.magic=blob.fast ? net::FastMagic : net::Magic;
    result.size=unsigned(blob.bytes.size());result.index=index;
    const auto offset=index*net::ChunkBytes;
    std::copy_n(blob.bytes.begin()+offset,(std::min)(size_t(net::ChunkBytes),blob.bytes.size()-offset),result.bytes.begin());
    return result;
}
int main(int argc,char** argv) {
    CC_CASE("標準36色から代表色を特定し、重複RGB・未使用色・鉛筆の影響を区別する");
    std::array<Palette,36> standards{};std::array<uint32_t,36> swatches{};
    std::array<unsigned,256> usage{};usage[40]=100;usage[42]=1000;
    for(unsigned i=0;i<36;++i) {
        swatches[i]=0x103080u+i*0x040201u;
        standards[i][20]=standards[i][40]=swatches[i];
    }
    standards[0][42]=swatches[0];
    CC_CHECK_EQ(RepresentativeIndex(standards,swatches,usage),40u);
    for(unsigned i=0;i<36;++i)standards[i][40]+=0x010101;
    CC_CHECK_EQ(RepresentativeIndex(standards,swatches,usage),40u); // 完全一致でも未使用の20番は選ばない。
    Edit sampling;sampling.palette[40]=0xff112233;
    const std::array<uint32_t,5> keys{100,101,102,103,104};
    CC_CHECK_EQ(RepresentativeColor(sampling,40,keys),0xff112233u);
    sampling.pixels[100]=0xff773355;
    CC_CHECK_EQ(RepresentativeColor(sampling,40,keys),0xff112233u);
    sampling.pixels[101]=sampling.pixels[102]=0xff773355;
    CC_CHECK_EQ(RepresentativeColor(sampling,40,keys),0xff773355u);
    for(auto key:keys)sampling.pixels[key]=0;
    CC_CHECK_EQ(RepresentativeColor(sampling,40,keys),0xff112233u);
    CC_CASE("既存6行の7ページ目へ往復し、全42枠が重複なく並ぶ");
    for(unsigned row=0;row<ColorRows;++row) {
        CC_CHECK_EQ(ColorPageMove(30+row,true),36+row);
        CC_CHECK_EQ(ColorPageMove(36+row,true),row);
        CC_CHECK_EQ(ColorPageMove(row,false),36+row);
        CC_CHECK_EQ(ColorPageMove(36+row,false),30+row);
    }
    for(unsigned selected=0;selected<MenuColors;++selected) {
        std::array<bool,MenuColors> seen{};
        for(unsigned color=0;color<MenuColors;++color) {
            const auto rank=ColorRank(color,selected);
            CC_CHECK(rank<MenuColors && !seen[rank]);seen[rank]=true;
            CC_CHECK_EQ(rank%ColorRows,color%ColorRows);
            CC_CHECK_EQ(rank<ColorRows,color/ColorRows==selected/ColorRows);
        }
    }
    CC_CASE("ACTのRGB256色を772/768バイトで読み、失敗時には編集中の色を守る");
    Palette palette;for(unsigned i=0;i<256;++i)palette[i]=i|((255-i)<<8)|(i<<16);
    auto act=WriteAct(palette);Palette decoded{};std::string error;
    CC_CHECK_EQ(act.size(),772u);CC_CHECK(ReadAct(act,decoded,error));CC_CHECK(decoded==palette);
    CC_CHECK(ReadAct(std::span(act).first(768),decoded,error));CC_CHECK(decoded==palette);
    act[768]=2;CC_CHECK(!ReadAct(act,decoded,error));CC_CHECK(decoded==palette);
    act=WriteAct(palette);act[771]=5;CC_CHECK(!ReadAct(act,decoded,error));
    for(int i=1;i<argc;++i) {
        std::ifstream file(argv[i],std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});
        CC_CHECK(ReadAct(bytes,decoded,error));CC_CHECK(WriteAct(decoded)==bytes);
    }
    CC_CASE("複数色表・RGB置換・鉛筆・パートナーを損なわずに保存し、破損と重複を拒否する");
    ExtraColor color{22,35,{{0,123,{}},{1,456,{}}}};
    color.parts[0].edit.palette=palette;color.parts[1].edit.palette=palette;
    color.parts[0].edit.effects[21]=palette;color.parts[0].edit.replacements[0x112233]=0x775533;
    for(unsigned i=0;i<1500;++i)color.parts[0].edit.pixels[i]=0xff001177;
    auto encoded=EncodeExtra(color);ExtraColor copy;
    CC_CHECK(DecodeExtra(encoded,copy));CC_CHECK_EQ(copy.baseColor,35u);CC_CHECK_EQ(copy.parts.size(),2u);
    CC_CHECK(copy.parts[0].edit==color.parts[0].edit);CC_CHECK(copy.parts[1].edit==color.parts[1].edit);
    for(unsigned end:{0u,16u,30u,1000u,unsigned(encoded.size()-1)})CC_CHECK(!DecodeExtra(std::span(encoded).first(end),copy));
    encoded[40]^=1;CC_CHECK(!DecodeExtra(encoded,copy));CC_CHECK_EQ(copy.character,22u);
    color.parts.push_back(color.parts[0]);CC_CHECK(EncodeExtra(color).empty());color.parts.pop_back();
    color.parts[1].component=0;CC_CHECK(EncodeExtra(color).empty());color.parts[1].component=1;
    CC_CASE("オブジェクトの別色表・アルファ面・直接RGBの半透明を保つ");
    Asset asset;auto& page=asset.pages[0];page.banks.resize(65536);page.alpha.resize(65536);page.direct.resize(65536);
    page.valid[0]=2;page.banks[0]=21;page.indices[0]=8;page.alpha[0]=80;
    auto edit=color.parts[0].edit;edit.pixels.clear();edit.effects[21][8]=0xff998877;
    CC_CHECK_EQ(asset.Color(edit,0),0x50998877u);
    page.valid[1]=3;page.direct[1]=0x60112233;
    CC_CHECK_EQ(asset.Color(edit,1),0x60775533u);
    edit.pixels[1]=0xffabcdef;CC_CHECK_EQ(asset.Color(edit,1),0xffabcdefu);
    CC_CASE("カラー全体を可逆圧縮し、破損・過大な展開長・末尾の余分なデータを拒否する");
    const auto raw=EncodeExtra(color);auto packed=net::CompressColor(raw);
    CC_CHECK(packed.size()<raw.size());CC_CHECK(net::DecodeColor(packed,true,copy));
    CC_CHECK(EncodeExtra(copy)==raw);CC_CHECK(!net::DecodeColor(packed,false,copy));
    auto corrupt=packed;corrupt.back()^=1;CC_CHECK(!net::DecodeColor(corrupt,true,copy));
    corrupt=packed;corrupt.push_back(0);CC_CHECK(!net::DecodeColor(corrupt,true,copy));
    corrupt=packed;corrupt.pop_back();CC_CHECK(!net::DecodeColor(corrupt,true,copy));
    corrupt=packed;uint32_t oversized=MaxExtraBytes+1;std::memcpy(corrupt.data()+4,&oversized,4);
    CC_CHECK(!net::DecodeColor(corrupt,true,copy));CC_CHECK(EncodeExtra(copy)==raw);
    CC_CASE("UDPの欠落・逆順・重複では完成データを公開せず、古い世代も拒否する");
    auto blob=net::Make(65536,3,22,5,&color,9);CC_CHECK(bool(blob));
    net::Receiver receiver;const unsigned count=(blob->bytes.size()+net::ChunkBytes-1)/net::ChunkBytes;
    for(unsigned i=count;i-->1;){CC_CHECK(!receiver.Accept(Chunk(*blob,i)));CC_CHECK(!receiver.Accept(Chunk(*blob,i)));}
    CC_CHECK(!receiver.Get());CC_CHECK_EQ(receiver.Wanted(),0u);
    CC_CHECK(bool(receiver.Accept(Chunk(*blob,0))));CC_CHECK(receiver.Get()->bytes==blob->bytes);
    auto stale=net::Make(65536,2,22,UINT32_MAX,nullptr,8);CC_CHECK(!receiver.Accept(Chunk(*stale,0)));
    CC_CHECK_EQ(receiver.Get()->serial,3u);
    CC_CASE("再戦は受信済み画像を検証して再利用し、途中の色変更は全チャンクを待つ");
    auto newer=net::Make(131072,4,22,5,&color,1);CC_CHECK(bool(receiver.Accept(Chunk(*newer,0))));
    CC_CHECK(receiver.Get()->bytes==newer->bytes);CC_CHECK_EQ(receiver.Get()->epoch,131072u);
    auto modified=color;modified.parts[1].edit.palette[255]^=1;
    auto changed=net::Make(196608,5,22,5,&modified,2);
    CC_CHECK(!receiver.Accept(Chunk(*changed,0)));CC_CHECK(!receiver.Get());
    for(unsigned i=1;i<count;++i)receiver.Accept(Chunk(*changed,i));
    CC_CHECK(receiver.Get()->bytes==changed->bytes);
    CC_CHECK(!receiver.Accept(Chunk(*newer,0)));CC_CHECK_EQ(receiver.Get()->epoch,196608u);
    auto bad=Chunk(*newer,1);bad.index=UINT32_MAX;CC_CHECK(!receiver.Accept(bad));
    CC_CASE("同じキャラでも選択revisionとACKが一致するまで読込みを解放しない");
    net::Store::Reset();CC_CHECK(net::Store::Ready(65536,22,22,9,9));
    net::Store::Local(blob);CC_CHECK(!net::Store::Ready(65536,22,22,9,9));
    auto ack=Chunk(*blob,0);ack.ackEpoch=blob->epoch;ack.ackSerial=blob->serial;ack.ackHash=blob->hash;
    net::Store::Received(ack,blob);CC_CHECK(net::Store::Ready(65536,22,22,9,9));
    CC_CHECK(!net::Store::Ready(65536,22,22,9,10));CC_CHECK(!net::Store::Ready(65536,22,22,10,9));
    CC_CHECK(!net::Store::Ready(131072,22,22,9,9));
    net::Store::Local(net::Make(65536,4,22,0,&color,10));net::Store::Received(ack,blob);
    CC_CHECK(!net::Store::Ready(65536,22,22,10,9));
    CC_CASE("転送窓は相手の未受信位置へ進み、更新された色には即座に再開する");
    net::Exchange exchange;exchange.Reset();net::Store::Local(blob);
    net::Chunk outgoing;CC_CHECK(exchange.Next(outgoing,0));
    auto incoming=Chunk(*stale,0);incoming.ackEpoch=blob->epoch;incoming.ackSerial=blob->serial;incoming.wanted=12;
    exchange.Receive(incoming);CC_CHECK(exchange.Next(outgoing,100));CC_CHECK(outgoing.index>=12);
    net::Store::Local(newer);CC_CHECK(exchange.Next(outgoing,101));CC_CHECK_EQ(outgoing.index,0u);
    CC_CASE("旧通知の未使用末尾で圧縮転送を合意し、対応のない相手には従来形式を保つ");
    exchange.Reset();CC_CHECK(!net::Store::FastSupported());
    auto standard=net::Make(65536,20,22,UINT32_MAX,nullptr,4);
    auto legacy=Chunk(*standard,0);exchange.Receive(legacy);
    CC_CHECK(net::Store::Supported());CC_CHECK(!net::Store::FastSupported());
    CC_CHECK(exchange.Next(outgoing,0));CC_CHECK_EQ(outgoing.magic,net::Magic);
    uint32_t advertised=0;std::memcpy(&advertised,outgoing.bytes.data()+net::ChunkBytes-4,4);
    CC_CHECK_EQ(advertised,net::FastCapability);
    CC_CHECK_EQ(ColorHash(std::span(outgoing.bytes).first(12)),outgoing.hash);
    std::memcpy(legacy.bytes.data()+net::ChunkBytes-4,&net::FastCapability,4);
    exchange.Receive(legacy);CC_CHECK(net::Store::FastSupported());
    CC_CASE("受信設定は初期通知で独立に合意し、通知ACKまでカラー本体を公開しない");
    for(bool receive:{false,true})for(bool peerReceive:{false,true}) {
        exchange.Reset(receive);CC_CHECK(!net::Store::PolicyAcknowledged());
        CC_CHECK(exchange.Next(outgoing,0));CC_CHECK_EQ(outgoing.size,12u);
        uint32_t policy=0,allowed=2;
        std::memcpy(&policy,outgoing.bytes.data()+net::ChunkBytes-12,4);
        std::memcpy(&allowed,outgoing.bytes.data()+net::ChunkBytes-8,4);
        CC_CHECK_EQ(policy,net::ReceivePolicyCapability);CC_CHECK_EQ(allowed,unsigned(receive));
        auto reply=outgoing;allowed=peerReceive;
        std::memcpy(reply.bytes.data()+net::ChunkBytes-8,&allowed,4);
        exchange.Receive(reply);CC_CHECK(!net::Store::PolicyAcknowledged());
        reply.ackEpoch=outgoing.epoch;reply.ackSerial=outgoing.serial;reply.ackHash=outgoing.hash;
        exchange.Receive(reply);CC_CHECK(net::Store::PolicyAcknowledged());
        CC_CHECK_EQ(net::Store::PeerAcceptsColors(),peerReceive);CC_CHECK_EQ(net::Store::AcceptsColors(),receive);
        CC_CHECK_EQ(bool(net::Store::Peer()),receive);
    }
    CC_CASE("設定未対応の相手との受信拒否はカラーを展開せず、先頭選択情報だけでACKする");
    for(bool compressed:{false,true}) {
        exchange.Reset(false);auto legacyColor=net::Make(65536,90,22,0,&color,9,compressed);
        CC_CHECK(legacyColor->bytes.size()>net::ChunkBytes);
        exchange.Receive(Chunk(*legacyColor,1));CC_CHECK(exchange.Next(outgoing,0));CC_CHECK_EQ(outgoing.ackHash,0u);
        // 受信しない本体は不正な形式でもパーサへ渡さない。
        auto head=Chunk(*legacyColor,0);std::fill(head.bytes.begin()+12,head.bytes.end(),0xFF);
        exchange.Receive(head);CC_CHECK(exchange.Next(outgoing,1));CC_CHECK_EQ(outgoing.ackHash,legacyColor->hash);
        CC_CHECK_EQ(outgoing.ackSerial,90u);CC_CHECK(!net::Store::Peer());
        net::Store::Local(standard);head.ackEpoch=standard->epoch;head.ackSerial=standard->serial;head.ackHash=standard->hash;
        exchange.Receive(head);CC_CHECK(net::Store::Ready(65536,22,22,4,9));
        CC_CHECK(!net::Store::Ready(65536,22,22,4,10));
        for(unsigned i=1;i<(legacyColor->bytes.size()+net::ChunkBytes-1)/net::ChunkBytes;++i)exchange.Receive(Chunk(*legacyColor,i));
        CC_CHECK(!net::Store::Peer());
        auto invalid=head;invalid.serial=91;uint32_t badCharacter=101;std::memcpy(invalid.bytes.data(),&badCharacter,4);
        exchange.Receive(invalid);CC_CHECK(exchange.Next(outgoing,1000002));CC_CHECK_EQ(outgoing.ackSerial,90u);
        auto next=net::Make(131072,92,22,UINT32_MAX,nullptr,1,compressed);exchange.Receive(Chunk(*next,0));
        CC_CHECK(exchange.Next(outgoing,1000003));CC_CHECK_EQ(outgoing.ackHash,next->hash);
        exchange.Reset();CC_CHECK(net::Store::AcceptsColors());CC_CHECK(net::Store::PeerAcceptsColors());CC_CHECK(!net::Store::PolicyAcknowledged());
    }
    CC_CASE("選択確定でrevisionが変わっても同じ色の受信途中から継続する");
    auto fast=net::Make(65536,30,22,5,&color,9,true);
    auto confirmed=net::Make(65536,31,22,5,&color,10,true);
    CC_CHECK_EQ(fast->hash,confirmed->hash);
    const auto rebound=net::Rebind(*fast,65536,31,10);
    CC_CHECK(rebound->bytes==confirmed->bytes);CC_CHECK_EQ(rebound->hash,confirmed->hash);
    net::Receiver partial;const unsigned fastCount=(fast->bytes.size()+net::ChunkBytes-1)/net::ChunkBytes;
    for(unsigned i=0;i<fastCount/2;++i)CC_CHECK(!partial.Accept(Chunk(*fast,i)));
    CC_CHECK(!partial.Accept(Chunk(*confirmed,0)));CC_CHECK_EQ(partial.Wanted(),fastCount/2);
    for(unsigned i=fastCount/2;i<fastCount;++i)partial.Accept(Chunk(*confirmed,i));
    CC_CHECK(bool(partial.Get()));CC_CHECK(partial.Get()->bytes==confirmed->bytes);
    CC_CHECK_EQ(partial.Get()->Revision(),10u);
    CC_CASE("標準色を経由して再選択してもキャッシュを失わず、変更済みの別データは混ぜない");
    auto neutral=net::Make(131072,32,22,UINT32_MAX,nullptr,1,true);
    CC_CHECK(bool(partial.Accept(Chunk(*neutral,0))));
    auto reselected=net::Make(131072,33,22,5,&color,2,true);
    CC_CHECK(bool(partial.Accept(Chunk(*reselected,0))));CC_CHECK(partial.Get()->bytes==reselected->bytes);
    auto fastChanged=net::Make(131072,34,22,5,&modified,3,true);
    CC_CHECK(fastChanged->hash!=reselected->hash);CC_CHECK(!partial.Accept(Chunk(*fastChanged,0)));
    CC_CHECK(!partial.Get());CC_CHECK_EQ(partial.Wanted(),1u);
    for(unsigned i=1;i<(fastChanged->bytes.size()+net::ChunkBytes-1)/net::ChunkBytes;++i)partial.Accept(Chunk(*fastChanged,i));
    CC_CHECK(partial.Get()->bytes==fastChanged->bytes);
    CC_CASE("受信完了ACKは1秒のキープアライブ待ちをせず即時に返す");
    exchange.Reset();net::Store::Local(standard);
    for(unsigned i=0;i<fastCount-1;++i) {
        auto part=Chunk(*fast,i);part.ackEpoch=standard->epoch;part.ackSerial=standard->serial;part.ackHash=standard->hash;
        exchange.Receive(part);
    }
    CC_CHECK(net::Store::Acknowledged());CC_CHECK(exchange.Next(outgoing,100));CC_CHECK_EQ(outgoing.ackHash,0u);
    exchange.Receive(Chunk(*fast,fastCount-1));CC_CHECK(exchange.Next(outgoing,101));CC_CHECK_EQ(outgoing.ackHash,fast->hash);
    CC_CHECK(!exchange.Next(outgoing,102));
#ifdef _WIN32
    CC_CASE("日本語パスの6枠をキャラ別に保存し、不正保存時は既存ファイルを守る");
    const auto root=std::filesystem::temp_directory_path()/(L"cccaster_palette_検証_"+std::to_wstring(GetCurrentProcessId()));
    const auto utf8=root.u8string();cccaster::core::paths::SetDataRoot(std::string(utf8.begin(),utf8.end()));
    for(unsigned i=0;i<6;++i){color.baseColor=i;CC_CHECK(SaveExtra(22,i,color,error));ExtraColor read;CC_CHECK(LoadExtra(22,i,read,error));CC_CHECK_EQ(read.baseColor,i);}
    CC_CHECK(!LoadExtra(7,0,copy,error));CC_CHECK(!SaveExtra(22,6,color,error));
    auto invalid=color;invalid.character=7;CC_CHECK(!SaveExtra(22,0,invalid,error));
    CC_CHECK(LoadExtra(22,0,copy,error));CC_CHECK_EQ(copy.baseColor,0u);
    // 今回作った既知のファイルだけを削除する。既存ディレクトリの再帰削除は行わない。
    for(unsigned i=0;i<6;++i)std::filesystem::remove(ExtraPath(22,i));
    std::filesystem::remove(ExtraPath(22,0).parent_path());std::filesystem::remove(root/"extra_colors");std::filesystem::remove(root);
#endif
    return cccaster::test::Summarize("エクストラカラー入出力・通信");
}
