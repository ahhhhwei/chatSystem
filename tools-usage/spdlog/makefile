all : sync async main
main:main.cc
	g++ -o $@ $^ -std=c++17 -lspdlog -lfmt -DSPDLOG_FMT_EXTERNAL -pthread -lgflags
sync:sync.cc
	g++ -o $@ $^ -std=c++17 -lspdlog -lfmt -DSPDLOG_FMT_EXTERNAL -pthread
async:async.cc
	g++ -o $@ $^ -std=c++17 -lspdlog -lfmt -DSPDLOG_FMT_EXTERNAL -pthread
.PHONY:clean
clean:
	rm -rf sync async main