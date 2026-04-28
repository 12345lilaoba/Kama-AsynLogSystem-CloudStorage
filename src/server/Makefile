CXXFLAGS=-std=c++17
LDFLAGS=-lpthread -lstdc++fs -ljsoncpp -lbundle -levent
DEPS=$(wildcard *.hpp ../../log_system/logs_code/*.hpp)

test:Test.cpp base64.cpp $(DEPS)
	g++ -o $@ Test.cpp base64.cpp $(CXXFLAGS) $(LDFLAGS)
gdb_test:Test.cpp base64.cpp $(DEPS)
	g++ -g -o $@ Test.cpp base64.cpp $(CXXFLAGS) $(LDFLAGS)
.PHONY:clean
clean:
	rm -rf test gdb_test ./deep_storage ./low_storage ./logfile storage.data
