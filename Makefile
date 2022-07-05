ALL: dir snstats snqueues mnstats mnqueues eventtrace

dir:
	mkdir -p bin

snstats: dir
	$(CXX) -o bin/snstats src/snstats/SNStats.cpp \
			src/common/MemTraceReader.cpp src/common/util.cpp -Ofast -flto \
			-Wno-write-strings -std=c++17

snqueues: dir
	$(CXX) -o bin/snqueues src/snqueues/SNQueues.cpp \
			src/common/MemTraceReader.cpp src/common/util.cpp -Ofast -flto \
			-Wno-write-strings -std=c++17

mnstats: dir
	$(CXX) -o bin/mnstats src/mnstats/MNStats.cpp \
			src/mnstats/Node.cpp src/mnstats/Page.cpp \
			src/common/MemTraceReader.cpp src/common/util.cpp -Ofast -flto \
			-Wno-write-strings -std=c++17

mnqueues: dir
	$(CXX) -o bin/mnqueues src/mnqueues/MNQueues.cpp \
			src/common/MemTraceReader.cpp src/common/util.cpp -Ofast -flto \
			-Wno-write-strings -std=c++17

eventtrace: dir
	$(CXX) -o bin/eventtrace src/eventtrace/EventTrace.cpp src/common/util.cpp \
			-Ofast -flto -Wno-write-strings -std=c++17

clean:
	rm -rf bin
