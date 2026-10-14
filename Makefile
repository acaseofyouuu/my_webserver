server: main.cpp config.cpp config.h webserver.cpp webserver.h http/http_conn.cpp http/http_conn.h threadpool/thread_pool.cpp threadpool/thread_pool.h log/logger.cpp log/logger.h database/database_pool.cpp database/database_pool.h database/password_hash.cpp database/password_hash.h database/account_repository.cpp database/account_repository.h session/session_manager.cpp session/session_manager.h
	g++ -std=c++17 -Wall -Wextra -pthread main.cpp config.cpp webserver.cpp http/http_conn.cpp threadpool/thread_pool.cpp log/logger.cpp database/database_pool.cpp database/password_hash.cpp database/account_repository.cpp session/session_manager.cpp -lmysqlclient -lcrypto -o server

clean:
	rm -f server

.PHONY: clean