#include "simulator.h"
#include "config.h"
#include <pthread.h>
#include <time.h>
#include <queue>
#include <iostream>
#include <vector>

static void * pipelinerSimulatorMain( void * arg );

// This is a crazy hack as I am too lazy to figure out how to pass this pointer
// correctly into the threads, so in shmem it sits. Hope I fix this before I 
// "release" this.
Simulator * shmemSimulatorHandle = nullptr;

void Simulator::resetControlSignals() {
    controlSignals = std::vector< int >();
    for ( int i = 0; i < config->numStages(); i++ ) {
        controlSignals.push_back( -1 );
    }
}

void Simulator::setUpTimeSpecs() {
    timespecs = std::vector< struct timespec >( config->numStages() );
    for ( int i = 0; i < config->numStages(); i++ ) {
        timespecs[ i ].tv_sec = 0;
        timespecs[ i ].tv_nsec = 
            ( config->baseDelay() + config->imbalanceFactor()[ i ] ) 
            * microSecondMultiplier;
    }
}

Simulator::Simulator( Config * config ) {
    this->config = config;
}

/*
 * The pipelined case is a bit complicated for filling up the work queue. There
 * are two main stages: steady state operation setup, and pipeline draining 
 * opertaion setup. The rest of this comment will explain the pipelined case.
 *
 * During the first stage we calculate how many work items should be put into 
 * each stage but the last. In order to optimally fill the pipeline, we need to 
 * emplace an extra maxPipelineCapacity % numStages work items in the last 
 * stage. This way, during steady state pipeline operation, we always have the
 * pipeline running at max capacity without ever overfilling the pipeline.
 *
 * During the second stage the work queue filling is a bit more complex. We need
 * to pack numWorkItems % maxPipelineCapacity work items in as few queue spots
 * as possible while also never overfilling the pipeline. This is a bit tricky
 * and I cannot guarantee optimality of my approach, but the approach is
 * correct. The approach is to first add remainderOfWork / numStages work queue
 * elements of remainderOfWork / numStages work items each. Then, there still
 * might be remainderOfWork % numStages work items left to process, and I fill
 * the work queue with remainderOfWork % numStages 1s. 
 *
 * The reason that the second stage will never overfill the pipeline is as 
 * follows:
 *
 * At the end of the first stage, the pipeline is at max capacity, with the next
 * item to be falling off the end of the pipeline being of value of 
 * maxPipelineCapacity / numStages. 
 *
 * At the start of the second stage, the first item being inserted has a maximum
 * value of max( ( numWorkItems % maxPipelineCapacity ) / numStages, 1 ). Now, 
 * since my pipeline implementation does not support bubbles, 
 * maxPipelineCapacity / numStages >= 1, so the second expression in the max
 * equation is safe.
 *
 * For the first expression in the max equation, it is enough to see that since
 * numWorkItems % maxPipelineCapacity has a range of 
 * [ 0, maxPipelineCapacity - 1 ], the first expression in the max equation can
 * never be greater than maxPipelineCapacity / numStages, so you can never 
 * overfill the pipeline.
 *
 * QED.
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
    while ( !workItems.empty() ) {
        int currentWorkItems = workItems.front();
        workItems.pop();

        for ( int stage = 0; stage < config->numStages(); stage++ ) {
            for ( int workItem = 0; workItem < currentWorkItems; workItem++ ) {
                // "Process" the work item
                // In the case of the simulator, you "process" by sleeping for
                // a specified amount of time. 
                nanosleep( &( timespecs[ stage ] ), NULL );
            }
        }
    }
}

void Simulator::noPipelinerDriver( bool shortCircuit ) {
    setUpWorkQueueForConfig( false );

    std::cout << "Starting" << ( shortCircuit ? " " : " non " ) 
        << "pipelined simulation" << std::endl;

    auto startTimer = std::chrono::high_resolution_clock::now();
    noPipelinerSimulation();
    auto endTimer = std::chrono::high_resolution_clock::now();

    // Did you know that this below line of code is perfectly legal? I think it
    // shouldn't be, but at this point I am arguing with the C spec.
    ( shortCircuit ? durationPipelined : durationNonPipelined ) = endTimer 
        - startTimer;

    // Print out the results. 
    std::cout << "\t" << ( shortCircuit ? "Pipelined" : "Non pipelined" ) 
        << " time taken: " << ( shortCircuit ? durationPipelined.count() 
                : durationNonPipelined.count() ) 
        << std::endl;
    std::cout << "\tThroughput: " 
        << config->numWorkItems() / ( ( shortCircuit ? durationPipelined.count()
                    : durationNonPipelined.count() ) / 1000 )
        << " work items per second" << std::endl;
}
            
void Simulator::simulatorMain() {
    setUpTimeSpecs();

    // Run the non pipelined simulation first.
    if ( !config->skipNoPipeline() ) {
       noPipelinerDriver( false ); 
    }

    // If only one stage is provided, it's semantically equivalent to the non
    // pipelined implementaion. 
    if ( config->numStages() == 1 ) {
        noPipelinerDriver( true );
        std::cout << "Providing speedup data is not supported for a single"
            << " stage pipeline" << std::endl;
        return;
    }

    // This is part of the horribly disgusting hack I mentioned on line 11
    shmemSimulatorHandle = this;

    // Setup for the threaded pipelined system run.
    resetControlSignals();
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
    std::cout << "\tThroughput: " 
        << config->numWorkItems() / ( durationPipelined.count() / 1000 )
        << " work items per second" << std::endl;

    if ( config->skipNoPipeline() ) {
        std::cout << "Cannot provide speedup information because the run "
            << "without pipelining was skipped" << std::endl;
    } else {
        double speedupRatio = durationNonPipelined.count()
            / durationPipelined.count();
        std::cout << "The pipelined implementation ran " << speedupRatio 
            << " times faster than the non pipelined implementation." 
            << std::endl;
    }
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
    if ( tid == shmemSimulatorHandle->controlThread ) {
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
        shmemSimulatorHandle->simulateStage( tid );

        // Wait until all stages finish executing.
        pthread_barrier_wait( &( shmemSimulatorHandle->barrier ) );

        // Part 2: Control the pipeline.
        if ( tid == shmemSimulatorHandle->controlThread ) {
            shmemSimulatorHandle->controlPipeline();
        }
        
        // Wait until all stage execution is set up again.
        pthread_barrier_wait( &( shmemSimulatorHandle->barrier ) );
    }

    // This return is to get rid of a compiler warning.
    return 0;
}

void Simulator::simulateStage( int tid ) {
    // Only let the stage run if it's control is enabled.
    if ( controlSignals[ tid ] != 0 ) {
        return;
    }

    // Assume control set up inputs for this stage already.
    int currentWorkItems = stageInputs[ tid * falseSharingPreventionBuffer ];

    for ( int workItem = 0; workItem < currentWorkItems; workItem++ ) {
        // "Process" the work item.
        // In the case of the simulator, you "process" by sleeping for a 
        // specified amount of time.
        nanosleep( &( timespecs[ tid ] ), NULL );
    }

    // Set the stage output for control to pass to the next stage as input.
    stageOutputs[ tid * falseSharingPreventionBuffer ] = currentWorkItems;
}

void Simulator::controlPipeline() {
    // At this point, all stages *must* have processed their inputs and 
    // generated their outputs. Therefore, we need to move these outputs to be
    // inputs and change the control signals as necessary. Interesting thing, 
    // this stage does not care who the control thread is, it simply needs to
    // be called by a single thread only to avoid race conditions.

    dumpDebugInfo( 0 );

    // First control stage: Check if there are more work items to process in 
    // the queue. If there are work items left to process, then give them to the
    // first stage.
    if ( workItems.empty() ) {
        // The first stage is done processing.
        controlSignals[ 0 ] = 1;
        stageInputs[ 0 ] = 0;
    } else {
        stageInputs[ 0 ] = workItems.front();
        workItems.pop();
    }

    // Second control stage: Put previous stage outputs as the inputs for the 
    // next stage for each stage.
    for ( int stage = 1; stage < config->numStages(); stage++ ) {
        stageInputs[ stage * falseSharingPreventionBuffer ] = 
            stageOutputs[ ( stage - 1 ) * falseSharingPreventionBuffer ];
    }

    // Third control stage: Clean up all the stage outputs. Note that this is
    // the only loop that starts from 0 in this function!
    for ( int stage = 0; stage < config->numStages(); stage++ ) {
        stageOutputs[ ( stage ) * falseSharingPreventionBuffer ] = 0;
    }

    // Fourth control stage: Start the pipeline stages if they are not started
    // yet but have inputs to process.
    for ( int stage = 1; stage < config->numStages(); stage++ ) {
        if ( controlSignals[ stage ] == -1 
                && stageInputs[ stage * falseSharingPreventionBuffer ] != 0 ) {
            controlSignals[ stage ] = 0;
        }
    }

    // Fifth control stage: If there is no input to process for a stage that has
    // been started before, then turn off the stage.
    for ( int stage = 1; stage < config->numStages(); 
            stage++ ) {
        if ( controlSignals[ stage ] == 0
                && stageInputs[ stage * falseSharingPreventionBuffer ] == 0 ) {
            controlSignals[ stage ] = 1;
        }
    }

    // Sixth control stage: If the last pipeline stage is finished processing 
    // everything, then signal to the "event" loop to break.
    if ( controlSignals[ config->numStages() - 1 ] == 1 ) {
        leaveEventLoop = true;
    }

    // Pipeline control is done.
    dumpDebugInfo( 1 );
}
