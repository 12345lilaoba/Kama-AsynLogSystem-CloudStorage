MYSQL_CFLAGS=$(shell mysql_config --cflags 2>/dev/null)
MYSQL_LDFLAGS=$(shell mysql_config --libs 2>/dev/null)
CXXFLAGS=-std=c++17 $(MYSQL_CFLAGS)
LDFLAGS=-lpthread -lstdc++fs -ljsoncpp -levent $(MYSQL_LDFLAGS)
DEPS=$(wildcard *.hpp ../../log_system/logs_code/*.hpp)

test:Test.cpp base64.cpp $(DEPS)
	g++ -o $@ Test.cpp base64.cpp $(CXXFLAGS) $(LDFLAGS)
gdb_test:Test.cpp base64.cpp $(DEPS)
	g++ -g -o $@ Test.cpp base64.cpp $(CXXFLAGS) $(LDFLAGS)
.PHONY:clean
clean:
	rm -rf test gdb_test ./storage ./logfile
