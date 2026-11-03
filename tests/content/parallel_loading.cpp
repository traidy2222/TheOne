#include <theone/scene/world.hpp>
#include "assets/cache/prepared_cache.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <set>
#include <cstdlib>
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void workers(const char* count) {
#ifdef _WIN32
    _putenv_s("THEONE_ASSET_WORKERS",count);
#else
    setenv("THEONE_ASSET_WORKERS",count,1);
#endif
}
int main(int argc,char** argv) {
    try {
        require(argc==3,"Expected scene and temporary directory");
        workers("1");auto serial=one::World::load(argv[1]);
        workers("2");one::SceneLoadStats stats;auto parallel=one::World::load(argv[1],&stats);
        require(serial.entities.size()==parallel.entities.size(),"Entity count changed");
        require(stats.assetWorkers>=1 && stats.assetWorkers<=2 && stats.assetWorkingEstimate<=128ull*1024*1024,"Admission limit or concurrency failed");
        std::set<uint64_t> identities;
        for(size_t i=0;i<serial.entities.size();++i) {
            const auto& a=serial.entities[i];const auto& b=parallel.entities[i];
            require(a.id==b.id && a.parent==b.parent,"Scene order changed");
            if(a.model) {
                require(b.model && a.model!=b.model && a.model->sourceFingerprint==b.model->sourceFingerprint && b.model->sourceFingerprint,"Fingerprint or fresh identity changed");
                require(one::asset_cache::encode(*a.model,{})==one::asset_cache::encode(*b.model,{}),"Parallel prepared payload differs");
                identities.insert(b.model->handle.id);
                for(size_t j=0;j<i;++j)if(parallel.entities[j].asset==b.asset)require(parallel.entities[j].model==b.model,"Repeated asset not shared");
            }
        }
        require(identities.size()==stats.models.models,"Runtime identity collision");
        std::stop_source cancelled;cancelled.request_stop();bool stopped=false;
        try{one::World::load(argv[1],nullptr,cancelled.get_token());}catch(...){stopped=true;}
        require(stopped,"Cancelled scene published");
        std::ifstream input(argv[1]);nlohmann::json j;input>>j;
        for(auto& e:j["entities"])if(e.contains("asset"))e["asset"]=(std::filesystem::absolute(argv[1]).parent_path()/e["asset"].get<std::string>()).generic_string();
        j["entities"][0]["asset"]="missing-model.glb";
        auto root=std::filesystem::path(argv[2]);std::filesystem::create_directories(root);
        auto broken=root/"missing.one";{std::ofstream out(broken);out<<j;}
        bool failed=false;try{one::World::load(broken);}catch(...){failed=true;}
        require(failed,"Failed asset published partial scene");
        std::cout<<"PASS parallel/serial bytes, deterministic entity order, identities, deduplication, admission budget, cancellation, failure\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
