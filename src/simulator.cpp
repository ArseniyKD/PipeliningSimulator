#include "simulator.h"
#include "config.h"
#include <pthread.h>
#include <time.h>
#include <queue>
#include <iostream>
#include <vector>

int const controlThread = 0;
int const microSecondMultiplier = 1000;
int const falseSharingPreventionBuffer = 10;

static void * pipelinerSimulatorMain( void * arg );
static void simulateStage( int tid );
static void controlPipeline();

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
            * microSecondMultiplier;
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
            for ( int j = 0; j < numStages - 1; j++ ) {
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
            for ( int i = 0; i < perStageWorkItemsIterations; i++ ) {
                workItems.push( perStageWorkItemsIterations );
            }
            for ( int i = 0; i < remainderOfWork % numStages; i++ ) {
                workItems.push( 1 );
            }
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

    // Implementing this right now under the assumption that it's 2+ stages. 
    // Will short-circuit 1 stage later ( note for future self: 1 stage is 
    // semantically equivalent to the non-pipelined run ).

    // This is part of the horribly disgusting hack I mentioned on line 11:
    shmemSimulatorHandle = this;

    // Setup for the threaded pipelined system run.
    resetControlSignals( this );
    setUpWorkQueueForConfig( true );
    TID = std::vector< pthread_t >( config->numStages() );
    stageInputs = std::vector< int >( 
            config->numStages() * falseSharingPreventionBuffer, 0 );
    stageOutputs = std::vector< int >( 
            config->numStages() * falseSharingPreventionBuffer, 0 );

    pthread_barrier_init( &barrier, NULL, config->numStages() );
    pthread_setconcurrency( config->numStages() );
    int shortID[ config->numStages() ];

    for ( int i = 1; i < config->numStages(); i++ ) {
        shortID[ i ] = i;
        pthread_create( &TID[ i ], NULL, pipelinerSimulatorMain, 
                        &shortID[ i ] );
    }

    shortID[ 0 ] = 0;

    std::cout << "Starting pipelined simulation" << std::endl;
    
    // Time the simulation run from the "control" thread perspective.
    auto startTimer = std::chrono::high_resolution_clock::now();
    pipelinerSimulatorMain( ( void * ) &( shortID[ 0 ] ) );
    auto endTimer = std::chrono::high_resolution_clock::now();
    durationPipelined = endTimer - startTimer;

    pthread_barrier_destroy( &barrier );
    
    // Print out the results. 
    std::cout << "\tPipelined time taken: " 
        << durationPipelined.count() << std::endl;
    std::cout << "\tBandwidth: " 
        << config->numWorkItems() / ( durationPipelined.count() / 1000 )
        << " wips" << std::endl;
}

void Simulator::dumpDebugInfo( int state ) {
    if ( !debug ) { 
        return;
    }
    std::cout << "DEBUG INFO DUMP AT " << ( state ? "END" : "START" ) << " OF "
        << "CONTROL" << std::endl;
    std::cout << "\t stage inputs:";
    for ( int i = 0; i < config->numStages(); i++ ) {
        std::cout << " " << stageInputs[ i * falseSharingPreventionBuffer ];
    }
    std::cout << std::endl;
    std::cout << "\t stage outputs:";
    for ( int i = 0; i < config->numStages(); i++ ) {
        std::cout << " " << stageOutputs[ i * falseSharingPreventionBuffer ];
    }
    std::cout << std::endl;
    std::cout << "\t controlSignals:";
    for ( int i = 0; i < config->numStages(); i++ ) {
        std::cout << " " << controlSignals[ i ];
    }
    std::cout << std::endl;
    std::cout << "\t leaveEventLoop: " << ( leaveEventLoop ? "true" : "false" )
        << std::endl;
    if ( state ) {
        std::cout << "----------ITERATION END----------" << std::endl;
    }
}

static void * pipelinerSimulatorMain( void * arg ) {
    int tid = *( int * ) arg;

    // Wait for all the threads to gather.
    pthread_barrier_wait( &( shmemSimulatorHandle->barrier ) );

    // Pipeline initialization done by the control thread only, the rest 
    // can enter the main body loop.
    if ( tid == controlThread ) {
        shmemSimulatorHandle->stageInputs[ 0 ] = 
            shmemSimulatorHandle->workItems.front();
        shmemSimulatorHandle->workItems.pop();
        shmemSimulatorHandle->controlSignals[ 0 ]++;
    }

    // The logic for signaling to every thread that they should all break out
    // of the "event loop" is handled by the control thread only. Also, because
    // of barrier semantics, all threads *must* exit on the same iteration, as
    // otherwise we can have some threads that never wake while waiting at a 
    // barrier. 
    while ( !shmemSimulatorHandle->leaveEventLoop ) {
        // Part 1: Let each thread execute it's stage.
        simulateStage( tid );

        // Wait until all stages finish executing.
        pthread_barrier_wait( &( shmemSimulatorHandle->barrier ) );

        // Part 2: Control the pipeline.
        if ( tid == controlThread ) {
            controlPipeline();
        }
        
        // Wait until all stage execution is set up again.
        pthread_barrier_wait( &( shmemSimulatorHandle->barrier ) );
    }

    return 0;
}

static void simulateStage( int tid ) {
    // Only let the stage run if it's control is enabled.
    if ( shmemSimulatorHandle->controlSignals[ tid ] != 0 ) {
        return;
    }

    // Assume control set up inputs for this stage already.
    int currentWorkItems = shmemSimulatorHandle->stageInputs[ 
        tid * falseSharingPreventionBuffer ];

    for ( int workItem = 0; workItem < currentWorkItems; workItem++ ) {
        // "Process" the work item.
        // In the case of the simulator, you "process" by sleeping for a 
        // specified amount of time.
        nanosleep( &( shmemSimulatorHandle->timespecs[ tid ] ), NULL );
    }

    // Set the stage output for control to pass to the next stage as input.
    shmemSimulatorHandle->stageOutputs[ tid * falseSharingPreventionBuffer ] = 
        currentWorkItems;
}

static void controlPipeline() {
    // At this point, all stages *must* have processed their inputs and 
    // generated their outputs. Therefore, we need to move these outputs to be
    // inputs and change the control signals as necessary. Interesting thing, 
    // this stage does not care who the control thread is, it simply needs to
    // be called by a single thread only to avoid race conditions.

    // First control stage: Check if there are more work items to process in 
    // the queue. If there are work items left to process, then give them to the
    // first stage.

    shmemSimulatorHandle->dumpDebugInfo( 0 );

    if ( shmemSimulatorHandle->workItems.empty() ) {
        // The first stage is done processing.
        shmemSimulatorHandle->controlSignals[ 0 ] = 1;
        shmemSimulatorHandle->stageInputs[ 0 ] = 0;
    } else {
        shmemSimulatorHandle->stageInputs[ 0 ] =
            shmemSimulatorHandle->workItems.front();
        shmemSimulatorHandle->workItems.pop();
    }

    // Second control stage: Put previous stage outputs as the inputs for the 
    // next stage for each stage.
    for ( int stage = 1; stage < shmemSimulatorHandle->config->numStages(); 
            stage++ ) {
        shmemSimulatorHandle->stageInputs[ 
            stage * falseSharingPreventionBuffer 
        ] = shmemSimulatorHandle->stageOutputs[ 
                ( stage - 1 ) * falseSharingPreventionBuffer 
            ];
    }

    // Third control stage: Clean up all the stage outputs
    for ( int stage = 0; stage < shmemSimulatorHandle->config->numStages(); 
            stage++ ) {
        shmemSimulatorHandle->stageOutputs[ 
            ( stage ) * falseSharingPreventionBuffer 
        ] = 0;
    }

    // Fourth control stage: Start the pipeline stages if they are not started
    // yet but have inputs to process.
    for ( int stage = 1; stage < shmemSimulatorHandle->config->numStages(); 
            stage++ ) {
        if ( shmemSimulatorHandle->controlSignals[ stage ] == -1 
                && shmemSimulatorHandle->stageInputs[ 
                    stage * falseSharingPreventionBuffer 
                ] != 0 ) {
            shmemSimulatorHandle->controlSignals[ stage ] = 0;
        }
    }

    // Fifth control stage: If there is no input to process for a stage that has
    // been started before, then turn off the stage.
    for ( int stage = 1; stage < shmemSimulatorHandle->config->numStages(); 
            stage++ ) {
        if ( shmemSimulatorHandle->controlSignals[ stage ] == 0
                && shmemSimulatorHandle->stageInputs[ 
                    stage * falseSharingPreventionBuffer
                ] == 0 ) {
            shmemSimulatorHandle->controlSignals[ stage ] = 1;
        }
    }

    // Sixth control stage: If the last pipeline stage is finished processing 
    // everything, then signal to the "event" loop to break.
    if ( shmemSimulatorHandle->controlSignals[ 
            shmemSimulatorHandle->config->numStages() - 1
         ] == 1 ) {
        shmemSimulatorHandle->leaveEventLoop = true;
    }

    // Pipeline control is done.
    shmemSimulatorHandle->dumpDebugInfo( 1 );
}



    




