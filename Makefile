ALL:
	mkdir -p bin
	$(CXX) -o bin/traceproc traceproc.cpp util.cpp -Ofast -flto -Wno-write-strings

clean:
	rm -rf bin
