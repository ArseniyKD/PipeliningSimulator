# PipeliningSimulator
## _A generic pipelining epxloration tool written in C++_

PipeliningSimulator is a highly configurable, yet simple to use simulator intended to introduce people to the concept of pipelining. The simulator is written in pure C++ with pthreads, so you can explore the code and learn a lot about how you could implement a project like this yourself with minimal knowledge of C++! This project was made as supplementary material for my [YouTube video on the topic of pipelining](https://youtu.be/yYtNTJoYj0A). 

This README is split into multiple sections beyond this introduction.

1. [Feature List](#Feature-List)
2. [Configuration Manual](#Configuration-Manual)
3. [Installation And User Manual](#Installation-And-User-Manual)
4. [Code Navigation Manual](#Code-Navigation-Manual)
5. [Simulator Limitations](#Simulator-Limitations)
6. [Future Work](#Future-Work)

The simulator will first run the non pipelined version of the configuration provided, then run the system with pipelining enabled. Timings for each run will be provided, and as long as the non pipelined run was not skipped and the number of pipeline stages is more than one, speedup value will be provided as well.

Let me just quickly mention why I implemented the simulator the way I did. This is a simulator, because simulation adds noise that simply does not exist in pure theoretical models for pipelining. This noise is somewhat representative of the overheads associated with actually implementing a pipeline. 

Additionally, I went with central control for the pipeline controller not just because it is far easier to both implement and understand, but also to have a much more realistic "feel" to the results. After all, the trade off between central and distributed pipeline control comes down to implementation complexity vs. maximum system throughput improvement. 

Lastly, each work item "processing" is implemented as a Unix syscall to `nanosleep` because sleeping for a provided amount of time at each stage is a very reasonable abstraction for work item processing. With that out of the way, let's talk about the features of the simulator.

## Feature List
Since this simulator was intended to be as generic as possible, it supports the following features:
- As many pipeline stages as your computer can handle (note: each pipeline stage is a pthread).
- Pipeline stages can be unbalanced in whatever configuration your heart desires.
- Configurable number of work items to be processed.
- Configurable amount of time to process each work item.
- Sane default configuration that provides insightful results while only running for a total of 4.5 seconds.
- Well documented simulator code that can be insightful to explore.
- An easy simulation configuration method.
- Clear error messages for invalid configurations. 

With these features in mind, let's move on to the configuration manual.

## Configuration Manual

You can provide custom configurations for each simulation by simply providing a simple to use and maintain config file. 
The config file is a simple text file with the following format:

```
# Specifying the number of stages for the simulation

numStages <integer number>

# Specifying the base sleep time in micro seconds per work item

baseDelay <integer number>

# Specifying the total number of work items to process by the system

numWorkItems <integer number>

# Specifying the maximum capacity of the pipeline

maxPipelineCapacity <integer number>

# Specifying per stage pipeline imbalance (number of stages and the length of the list must be the same)

imbalanceFactor <space separated list of integers>

# Specifying that you do not want to execute the non-pipelined run

skipNoPipeline
```

The configuration text file supports single line comments using the `#` symbol as the first symbol on the line.

I have provided better documentation of how each configuration parameter works along with their default values in this file: `sampleConfigs/BasicConfig.txt`, and there are a couple other sample configurations in the `sampleConfigs/` folder.

## Installation And User Manual

To install the simulator, simply clone the repository and call make from the root of the repository. Here is a sample command to do so (please do not run arbitrary commands from a README you found online, this command is here just for reference)
```
git clone https://github.com/ArseniyKD/PipeliningSimulator.git && cd PipeliningSimulator && make
```

The make command will put the resulting binary called `pipe-sim` into the `bin/` folder in the repository root. 

From there, you can invoke the simulator with this command (from the repository root): 
```
bin/pipe-sim [optional/path/to/configuration/file]
```
If no configuration file is provided, the default settings will be used. 

## Code Navigation Manual

The repository is structured in such a way that it is fairly easy to navigate the code. The `include/` directory contains the header files and the class definitions for the simulator and the configuration parser. The `src/` directory contains the source code for both the configuration parser and the simulator. 

The configuration parser code is not particularly interesting, it is simply implemented as an extremely rudimentary visitor with a semantic verification stage at the end. 

The simulator code is actually quite fun to explore. There are a lot of interesting things happening, and quite a bit of rather involved code. Most of the code is overdocumented for the sake of being easier to understand for those that are not yet very proficient in C++ and programming in general. There is even a correctness proof for my work queue setup algorithm that I ended up sneaking in. Additionally, there is one instance of some "dark art" C code that might be interesting even for somewhat experienced people to see.

If you wish to see some interesting code output, then consider running the simulator with the `DEBUG` environment variable set, but make sure to only use very few work items, as there will be _a lot_ of output.

## Simulator Limitations

There are a few minor and a few major limitations of this simulator:
1. The simulator only works in Linux due to the dependence on `pthread_barrier`. Mac is not supported, but any VM should work. WSL was not tested.
2. The makefile is incredibly rudimentary.
3. The pipeline control is a simple central controller instead of using distributed control.
4. I do not have work queue filling optimality guarantees, so the speedup may be suboptimal in some edge cases.

Some of these issues I might attempt to resolve over time, but some of them I likely will not.

## Future Work

No project is ever actually complete, so here is a couple work items in decreasing priority that I wish to get to at some point:
1. Port the simulator implementation to Mac OS / FreeBSD. (Main issue is with `pthread_barrier`)
2. Set up test automation for the project. (The simulator was written in a way that is particularly amenable to unit testing)
3. Prove work queue filling algorithm optimality, or come up with an optimal algorithm.
4. Improve the makefile

If I come up with more ways to improve the simulator, then I will add to this list. 
