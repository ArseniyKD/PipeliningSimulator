#include <iostream>
#include <vector>
#include <queue>
#include <unordered_map>
#include "config.h"

void dumpConfiguration( Config & config ) {
    std::cout << "numStages: " << config.numStages() << std::endl;
    std::cout << "numWorkItems: " << config.numWorkItems() << std::endl;
    std::cout << "imbalanceFactor: [";
    for ( int i = 0; i < config.imbalanceFactor().size(); i++ ) {
        std::cout << " " << config.imbalanceFactor()[ i ];
    }
    std::cout << " ]" << std::endl;
    std::cout << "maxPipelineCapacity: " << config.maxPipelineCapacity() 
        << std::endl;
    std::cout << "baseDelay: " << config.baseDelay() << std::endl;
}

int main( int argc, char** argv ) {
    if ( argc < 2 ) {
        std::cout << "Missing required argument.\n"
            << "Required arguments: <config file path>\n";
        return 1;
    }
    Config config( argv[ 1 ] );
    config.parseConfigFile();
    dumpConfiguration( config );
}
