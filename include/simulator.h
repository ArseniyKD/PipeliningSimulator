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
    int const controlThread = 0;
    int const microSecondMultiplier = 1000;

    Config * config;

    std::chrono::duration< double, std::milli > durationPipelined;
    std::chrono::duration< double, std::milli > durationNonPipelined;

    std::queue< int > workItems = std::queue< int >();
    std::vector< int > intermediateValues;
    std::vector< pthread_t > TID;
    std::vector< struct timespec > timespecs;
    std::vector< int > controlSignals;
    
    void setUpWorkQueueForConfig( bool pipe );
    void noPipelinerSimulation();
    void simulatorMain();

    Simulator( Config * config );
};


#endif
