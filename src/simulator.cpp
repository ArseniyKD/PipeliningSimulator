#include "simulator.h"
#include "config.h"
#include <pthread.h>
#include <time.h>
#include <queue>
#include <iostream>
#include <vector>

void * pipelinerSimulatorMain( void * arg );

// This is a crazy hack as I am too lazy to figure out how to pass this pointer
// correctly into the threads, so in shmem it sits. Hope I fix this before I 
// "release" this.
Simulator * shmemSimulatorHandle = nullptr;

static void resetControlSignals( Simulator * sim ) {
    sim->controlSignals = std::vector< int >();
    for ( int i = 0; i < sim->config->numStages(); i++ ) {
        sim->controlSignals.push_back( -1 );
    }
}

static void setUpTimeSpecs( Simulator * sim ) {
    sim->timespecs = std::vector< struct timespec >( sim->config->numStages() );
    for ( int i = 0; i < sim->config->numStages(); i++ ) {
        sim->timespecs[ i ].tv_sec = 0;
        sim->timespecs[ i ].tv_nsec = 
            ( sim->config->baseDelay() + sim->config->imbalanceFactor()[ i ] ) 
            * sim->microSecondMultiplier;
    }
}

Simulator::Simulator( Config * config ) {
    this->config = config;
}

/*
 * The pipelined case is a bit complicated for setting up the work queue.
 * Here is a "graphical" example. Let's assume 11 work items, 2 pipeline stages,
 * and max pipeline capacity of 5 items. The pipeline operation will be as
 * follows:
 *
 * t0: * *                -- Processing start --
 * t1: 2 *  processed: 0  -- Filling pipeline --
 * t2: 3 2  processed: 0  -- Steady state start --
 * t3: 2 3  processed: 2
 * t4: 3 2  processed: 5  -- Last steady state --
 * t5: 1 3  processed: 7  -- Drain start --
 * t6: * 1  processed: 10 -- Drain end --
 * t7: * *  processed: 11 -- Processing end --
 *
 * So, our work queue looks like this: 1 3 2 3 2 
 *                                             ^ <- ( 11 // 5 ) // 2
 *                                           ^   <- ( ( 11 // 5 ) // 2 ) + 5 % 2
 *                                         ^     <- ( 11 // 5 ) // 2
 *                                       ^       <- ( ( 11 // 5 ) // 2 ) + 5 % 2
 *                                     ^         <- ( ( 11 % 5 ) // 2 ) + ( ( 11 % 5 ) % 2 )
 * ( 11, 5, 2 are unique numbers, you should get the idea what parameters I 
 *   am referring to from the numbers. "//" is python3 style int division. )
 *
 * This may be an overcomplicated way to optimally fill the queue, but I am
 * tired and do not care. 
 *
 */
void Simulator::setUpWorkQueueForConfig( bool pipe ) {
    int numWorkItems = config->numWorkItems();
    int maxPipelineCapacity = config->maxPipelineCapacity();
    int numStages = config->numStages();

    int numIterations = numWorkItems / maxPipelineCapacity;

    // Fill the work queue for steady state operation.
    for ( int i = 0; i < numIterations; i++ ) {
        if ( pipe ) {
            int perStageWorkItemsIterations = maxPipelineCapacity / numStages;
            for ( int j = 0; j < perStageWorkItemsIterations - 1; j++ ) {
                workItems.push( perStageWorkItemsIterations );
            }
            workItems.push( perStageWorkItemsIterations + 
                    ( maxPipelineCapacity % numStages ) );
        } else {
            workItems.push( maxPipelineCapacity );
        }
    }

    // Fill the work queue for pipeline draining operation. 
    int remainderOfWork = numWorkItems % maxPipelineCapacity;

    if ( remainderOfWork ) {
        if ( pipe ) {
            int perStageWorkItemsIterations = remainderOfWork / numStages;
            for ( int i = 0; i < perStageWorkItemsIterations - 1; i++ ) {
                workItems.push( perStageWorkItemsIterations );
            }
            workItems.push( remainderOfWork % numStages );
        } else {
            workItems.push( remainderOfWork );
        }
    }
}

void Simulator::noPipelinerSimulation() {
    while ( !this->workItems.empty() ) {
        int currentWorkItems = this->workItems.front();
        this->workItems.pop();

        for ( int stage = 0; stage < this->config->numStages(); stage++ ) {
            for ( int workItem = 0; workItem < currentWorkItems; workItem++ ) {
                // "Process" the work item
                // In the case of the simulator, you "process" by sleeping for
                // a specified amount of time. 
                nanosleep( &( this->timespecs[ stage ] ), NULL );
            }
        }
    }
}
            
void Simulator::simulatorMain() {
    resetControlSignals( this );
    setUpTimeSpecs( this );

    // Run the non pipelined simulation first.
    if ( !config->skipNoPipeline() ) {
        setUpWorkQueueForConfig( false );

        std::cout << "Starting non pipelined simulation" << std::endl;

        auto startTimer = std::chrono::high_resolution_clock::now();
        noPipelinerSimulation();
        auto endTimer = std::chrono::high_resolution_clock::now();
        durationNonPipelined = endTimer - startTimer;

        // Print out the results. 
        std::cout << "\tNon pipelined time taken: " 
            << durationNonPipelined.count() << std::endl;
        std::cout << "\tBandwidth: " 
            << config->numWorkItems() / ( durationNonPipelined.count() / 1000 )
            << " wips" << std::endl;
    }
    
    // For now, that's it. Will figure the rest out later.
}       

