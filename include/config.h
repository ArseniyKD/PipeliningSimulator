#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <vector>
#include <sstream>

class Config {
  private:
    int numStages_ = 4;
    int numWorkItems_ = 10000;
    std::vector< int > imbalanceFactor_ = std::vector< int >();
    int maxPipelineCapacity_ = 100;
    int baseDelay_ = 20;
    std::string configFileName_;
    int visitedBitMap = 0;
    bool skipNoPipeline_ = false;
    void visit( std::istringstream & iss, int lineNum );
    void visitNumStages( std::istringstream & iss, int lineNum );
    void visitNumWorkItems( std::istringstream & iss, int lineNum );
    void visitMaxPipelineCapacity( std::istringstream & iss, int lineNum );
    void visitBaseDelay( std::istringstream & iss, int lineNum );
    void visitImbalanceFactor( std::istringstream & iss, int lineNum );
    void verifySemantics();
  public:
    Config( char * configFileName );
    void parseConfigFile();
    int numStages();
    int numWorkItems();
    int maxPipelineCapacity();
    int baseDelay();
    std::vector< int > imbalanceFactor();
    bool skipNoPipeline();
};

#endif
