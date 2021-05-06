#include "config.h"
#include <sstream>
#include <fstream>
#include <stdlib.h>
#include <iostream>
#include <string>

// Red Bold Underlined ANSII escape sequence start and end
static std::string const rbus = "\033[31;1;4m";
static std::string const rbue = "\033[0m"; 

Config::Config( char * configFileName ) {
    this->configFileName_ =  std::string( configFileName );
}

int Config::numStages() {
    return this->numStages_;
}

int Config::numWorkItems() {
    return this->numWorkItems_;
}

int Config::maxPipelineCapacity() {
    return this->maxPipelineCapacity_;
}

int Config::baseDelay() {
    return this->baseDelay_;
}

std::vector< int > Config::imbalanceFactor() {
    return this->imbalanceFactor_;
}

void Config::parseConfigFile() {
    std::ifstream infile( this->configFileName_ );

    if ( !infile.is_open() ) {
        std::cout << "File " << rbus << this->configFileName_ << rbue 
            << " could not be opened. Please check that this file exists." 
            << std::endl;
        abort();
    }

    std::string line;
    int lineNum = 0;
    while( std::getline( infile, line ) ) {
        std::istringstream iss( line );
        visit( iss, lineNum );
        lineNum++;
    }

    verifySemantics();
}

void Config::visit( std::istringstream & iss, int lineNum ) {
    std::string leadingString;
    
    if ( !( iss >> leadingString ) ) {
        // Read an empty line, ignore
        return;
    }
    
    if ( leadingString == "#" ) {
        return;
    } else if ( leadingString == "numStages" ) {
        visitNumStages( iss, lineNum );
    } else if ( leadingString == "numWorkItems" ) {
        visitNumWorkItems( iss, lineNum );
    } else if ( leadingString == "maxPipelineCapacity" ) {
        visitMaxPipelineCapacity( iss, lineNum );
    } else if ( leadingString == "baseDelay" ) {
        visitBaseDelay( iss, lineNum );
    } else if ( leadingString == "imbalanceFactor" ) {
        visitImbalanceFactor( iss, lineNum );
    } else { 
        std::cout << rbus << "Error:" << rbue << " Unrecognized configuration "
           << "option: " << rbus << leadingString << rbue << " at line: " 
           << lineNum << std::endl;
        std::cout << "Exiting program" << std::endl;
        abort();
    }
}

static int toInt( std::string value, int index ) {
    int retVal;
    try {
        retVal = std::stoi( value );
    } catch ( const std::invalid_argument &e ) {
        std::cout << rbus << "Error:" << rbue << " Encountered a non-integer "
            << "value " << rbus << value << rbue << " as token number " 
            << index << std::endl;
        abort(); 
    } catch ( const std::out_of_range &e ) {
        std::cout << rbus << "Error:" << rbue << " Encountered a value that "
            << " does not fit inside an integer: " << rbus << value << rbue 
            << " as token number " << index << std::endl;
        abort();
    }
    return retVal;
}
        

void Config::visitNumStages( std::istringstream & iss, int lineNum ) {
    if ( visitedBitMap & 0b1 ) {
        std::cout << rbus << "Error:" << rbue << " Specifying the numStages "
            << "configuration for the second time." << std::endl;
        abort();
    }

    std::string value; 
    if ( !( iss >> value ) ) {
        std::cout << rbus << "Error:" << rbue << " Nothing following the "
            << "numStages configuration keyword" << std::endl;
        abort();
    }

    this->numStages_ = toInt( value, 1 );
    visitedBitMap |= 0b1;
}

void Config::visitNumWorkItems( std::istringstream & iss, int lineNum ) {
    if ( visitedBitMap & 0b10 ) {
        std::cout << rbus << "Error:" << rbue << " Specifying numWorkItems " 
            << "configuration for the second time." << std::endl;
        abort();
    }

    std::string value;
    if ( !( iss >> value ) ) {
        std::cout << rbus << "Error:" << rbue << " Nothing following the "
            << "numWorkItems configuration keyword" << std::endl;
        abort();
    }

    this->numWorkItems_ = toInt( value, 1 );
    visitedBitMap |= 0b10;
}

void Config::visitMaxPipelineCapacity( std::istringstream & iss, int lineNum ) {
    if ( visitedBitMap & 0b100 ) { 
        std::cout << rbus << "Error:" << rbue << " Specifying "
            << "maxPipelineCapacity configuration for the second time." 
            << std::endl;
        abort();
    }
    std::string value;
    if ( !( iss >> value ) ) {
        std::cout << rbus << "Error:" << rbue << " Nothing following the "
            << "maxPipelineCapacity configuration keyword" << std::endl;
        abort();
    }

    this->maxPipelineCapacity_ = toInt( value, 1 );
    visitedBitMap |= 0b100;
}

void Config::visitBaseDelay( std::istringstream & iss, int lineNum ) {
    if ( visitedBitMap & 0b1000 ) {
        std::cout << rbus << "Error:" << rbue << " Specifying baseDelay "
            << "configuration for the second time." << std::endl;
        abort();
    }

    std::string value; 
    if ( !( iss >> value ) ) {
        std::cout << rbus << "Error:" << rbue << " Nothing following the "
            << "baseDelay configuration keyword" << std::endl;
        abort();
    }

    this->baseDelay_ = toInt( value, 1 );

    visitedBitMap |= 0b1000;
}

void Config::visitImbalanceFactor( std::istringstream & iss, int lineNum ) {
    if ( visitedBitMap & 0b10000 ) {
        std::cout << rbus << "Error:" << rbue << " Specifying imbalanceFactor "
            << "configuration for the second time." << std::endl;
        abort();
    }

    std::string value;
    int index = 1;
    while ( iss >> value ) {
        this->imbalanceFactor_.push_back( toInt( value, index++ ) );
    }

    visitedBitMap |= 0b10000;
}

void Config::verifySemantics() {
    // Make sure there are more than 0 stages.
    if ( numStages() < 1 ) {
        std::cout << rbus << "Error:" << rbue << " Fewer than 1 stage provided"
            << ", please provide a different number of stages than " 
            << numStages() << std::endl;
        abort();
    }

    // Zero out the imbalance factors if they were not specified. 
    if ( !( visitedBitMap & 0b10000 ) ) {
        for ( int i = 0; i < numStages(); i++ ) {
            this->imbalanceFactor_.push_back( 0 );
        }
    }

    // Make sure that the number of imbalance factors is the same as the 
    // number of stages.
    if ( imbalanceFactor().size() != numStages() ) {
        std::cout << rbus << "Error:" << rbue << " Number of imbalance factor "
            << "entries (" << imbalanceFactor().size() << ") is not the same as"
            << " the number of stages (" << numStages() << ")" << std::endl;
        abort();
    }

    // Verify that no negative time waiting can occur. 
    if ( visitedBitMap & 0b10000 ) {
        for ( int i = 1; i <= numStages(); i++ ) {
            if ( baseDelay() - imbalanceFactor()[ i - 1 ] < 0 ) { 
                std::cout << rbus << "Error:" << rbue << " The delay in stage "
                    << i << " is below 0. Either increase base delay, or "
                    << "decrease the imbalance factor for that stage."
                    << std::endl;
                abort();
            }
        }
    } 
}
