#ifndef SIMULATOR_H
#define SIMULATOR_H

#include "config.h"
#include <queue>
#include <chrono>
#include <vector>
#include <pthread.h>
#include <time.h>

class Simulator {
  public:
    Config * config;

    std::chrono::duration< double, std::milli > durationPipelined;
    std::chrono::duration< double, std::milli > durationNonPipelined;
    bool debug = false;

    std::queue< int > workItems = std::queue< int >();
    std::vector< int > stageInputs;
    std::vector< int > stageOutputs;
    std::vector< pthread_t > TID;
    std::vector< struct timespec > timespecs;
    std::vector< int > controlSignals;
    pthread_barrier_t barrier;
    bool leaveEventLoop = false;

    
    void setUpWorkQueueForConfig( bool pipe );
    void noPipelinerSimulation();
    void simulatorMain();
    void dumpDebugInfo( int state );

    Simulator( Config * config );
};


#endif
