#include "p2p/Matching.hpp"
#include "p2p/Ntfy.hpp"
#include <iostream>

int main(int argc,char** argv) {
    if(argc<2)return 2;
    try {
        if(argc==2 && std::string(argv[1])=="--directory-topic") {
            std::cout<<cccaster::matching::DirectoryTopic()<<'\n';
            return 0;
        }
        if(argc==3 && std::string(argv[2])=="seed" && std::string(argv[1]).rfind("http://127.0.0.1:",0)==0) {
            cccaster::p2p::Ntfy ntfy(argv[1]);
            for(unsigned i=1;i<=23;++i) {
                cccaster::matching::Identity identity;
                auto value=identity.Sign({{"v",1},{"kind","matching"},{"id",cccaster::matching::IdentityId(identity)},
                    {"code",cccaster::p2p::NewCode()},{"name","PAGE TEST "+std::to_string(i)},{"comment","GUI pagination fixture"},
                    {"spectators",true},{"revision",uint64_t(1)},{"action","register"},{"listed_at",cccaster::p2p::Now()}});
                if(ntfy.Post(cccaster::matching::DirectoryTopic(),value.dump()).status!=200)return 1;
            }
            return 0;
        }
        if(argc==3 && std::string(argv[2])=="seed-cleanup" && std::string(argv[1]).rfind("http://127.0.0.1:",0)==0) {
            cccaster::p2p::Ntfy ntfy(argv[1]);
            const auto now=cccaster::p2p::Now();
            const auto old=now-cccaster::matching::PublicListingLifetimeSeconds-100;
            for(const std::string name:{"EXPIRED","FRESH","RELISTED","LEGACY"}) {
                cccaster::matching::Identity identity;
                nlohmann::json value{{"v",1},{"kind","matching"},{"id",cccaster::matching::IdentityId(identity)},
                    {"code",cccaster::p2p::NewCode()},{"name",name},{"comment","Cleanup fixture"},
                    {"spectators",true},{"revision",uint64_t(1)},{"action","register"},{"created",old}};
                if(name!="LEGACY") value["listed_at"]=name=="EXPIRED"?old:now;
                if(ntfy.Post(cccaster::matching::DirectoryTopic(),identity.Sign(value).dump()).status!=200)return 1;
            }
            return 0;
        }
        cccaster::matching::Options options;
        options.server=argv[1];
        if(argc>2)options.responseSeconds=unsigned(std::stoi(argv[2]));
        cccaster::matching::Client client(options);
        std::atomic<bool> done=false;
        std::thread input([&] {
            std::string line;
            while(std::getline(std::cin,line)) {
                auto command=nlohmann::json::parse(line,nullptr,false);
                if(command.is_object())client.Command(std::move(command));
            }
            done=true;
        });
        std::string last;
        while(!done) {
            const auto v=client.View();
            nlohmann::json j{{"kind","snapshot"},{"registered",v.registered},{"public",v.publicVisible},
                {"code",v.code},{"id",v.id},{"state",v.state},{"notice",v.notice},{"error",v.error},
                {"notifications",v.notifications},{"incoming",nlohmann::json::array()},
                {"people",nlohmann::json::array()},{"outgoing",v.outgoing.id}};
            for(const auto& p:v.people)j["people"].push_back({{"id",p.id},{"code",p.code},{"name",p.name},{"result",p.result}});
            for(const auto& r:v.incoming)j["incoming"].push_back({{"id",r.id},{"code",r.peer.code},{"name",r.peer.name}});
            const auto text=j.dump();
            if(text!=last) {std::cout<<text<<'\n'<<std::flush;last=text;}
            for(const auto& e:client.Events())std::cout<<nlohmann::json({{"kind","event"},{"type",e.type},{"match",e.match},{"host",e.host},{"code",e.code},{"spectators",e.allowSpectators}}).dump()<<'\n'<<std::flush;
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
        input.join();
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
    return 0;
}
