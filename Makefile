server: main.cpp config.cpp config.h webserver.cpp webserver.h http/http_conn.cpp http/http_conn.h threadpool/thread_pool.cpp threadpool/thread_pool.h log/logger.cpp log/logger.h database/database_pool.cpp database/database_pool.h
	g++ -std=c++17 -Wall -Wextra -pthread main.cpp config.cpp webserver.cpp http/http_conn.cpp threadpool/thread_pool.cpp log/logger.cpp database/database_pool.cpp -lmysqlclient -o server

clean:
	rm -f server

.PHONY: clean