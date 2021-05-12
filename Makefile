CC=g++
INCLUDE_DIR=include
SRC_DIR=src
BIN_DIR=bin

LIBS=-lpthread
FLAGS=-O2 $(LIBS) -I$(INCLUDE_DIR)




all: src/config.cpp src/pipe-sim.cpp src/simulator.cpp 
	mkdir -p ./bin/
	$(CC) $(SRC_DIR)/config.cpp $(SRC_DIR)/pipe-sim.cpp $(SRC_DIR)/simulator.cpp -o $(BIN_DIR)/pipe-sim $(FLAGS)
