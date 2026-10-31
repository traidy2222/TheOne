#include "application.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
int main(int argc,char** argv) {
    try {
        Options options;
        for(int i=1;i<argc;++i) {
            std::string arg=argv[i];
            if(arg=="--smoke-test") options.smoke=true;
            else if(arg=="--validate") options.validate=true;
            else if(arg=="--scene" && i+1<argc) options.scene=argv[++i];
            else throw std::runtime_error("Unknown or incomplete option: "+arg);
        }
        return runApplication(options);
    } catch(const std::exception& e) { std::cerr<<"The One: "<<e.what()<<'\n'; return 1; }
}
