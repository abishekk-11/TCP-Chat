all:
	gcc -o main_server main_server.c -lpthread
	gcc -o main_client main_client.c -lpthread

clean:
	rm -f main_server main_client
