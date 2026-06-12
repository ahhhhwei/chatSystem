sync:sync.cc
	g++ -o $@ $^ -std=c++17 -lspdlog -lfmt -DSPDLOG_FMT_EXTERNAL -pthread
.PHONY:clean
clean:
	rm -rf sync