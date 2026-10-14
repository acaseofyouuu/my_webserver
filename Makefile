CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pthread
LDLIBS = -lmysqlclient -lcrypto
TARGET ?= server
SOURCES = main.cpp config.cpp webserver.cpp http/http_conn.cpp threadpool/thread_pool.cpp \
          log/logger.cpp database/database_pool.cpp database/password_hash.cpp \
          database/account_repository.cpp session/session_manager.cpp
HEADERS = $(wildcard *.h http/*.h threadpool/*.h log/*.h database/*.h session/*.h)

$(TARGET): $(SOURCES) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(SOURCES) $(LDLIBS) -o $@

clean:
	rm -f server server-sanitize

.PHONY: clean
