#include <iostream>
#include <vector>
#include <queue>
#include <unordered_map>
#include "config.h"
#include <cstdlib>
#include "simulator.h"

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
    std::cout << "skipNoPipeline: " << config.skipNoPipeline() << std::endl;
}

int main( int argc, char** argv ) {
    if ( argc > 2 ) {
        std::cout << "More than one argument provided, only one or less " 
            << "supported.\nCommand usage:\n\tbin/pipe-sim [optional/path/to"
            << "/configuration/file]" << std::endl;
        return 1;
    }
    char defaultFile[] = "/dev/null";
    Config config( ( argc < 2 ) ? defaultFile : argv[ 1 ] );
    config.parseConfigFile();
    bool debug = std::getenv( "DEBUG" );
    if ( debug ) {
        dumpConfiguration( config );
    }
    Simulator simulator( &config );
    simulator.debug = debug;
    simulator.simulatorMain();
}
