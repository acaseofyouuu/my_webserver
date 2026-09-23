server: main.cpp webserver.cpp webserver.h http/http_conn.cpp http/http_conn.h
	g++ -std=c++17 -Wall -Wextra main.cpp webserver.cpp http/http_conn.cpp -o server

clean:
	rm -f server

.PHONY: clean