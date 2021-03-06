CC=clang++
OPTS=-std=c++14 -Wall -Werror
SERVER_EXE=server
CLIENT_EXE=client
OBJ=# add *.o files here

all: $(SERVER_EXE) $(CLIENT_EXE)

$(SERVER_EXE): $(SERVER_EXE).cc $(OBJ)
	$(CC) $(OPTS) -o $(SERVER_EXE) $(SERVER_EXE).cc $(OBJ)

$(CLIENT_EXE): $(CLIENT_EXE).cc $(OBJ)
	$(CC) $(OPTS) -o $(CLIENT_EXE) $(CLIENT_EXE).cc $(OBJ)

# Copy for each *.o file
# example.o: src/example.cc
# 	$(CC) $(OPTS) -c src/example.cc

clean:
	rm -rf $(SERVER_EXE) $(CLIENT_EXE) *.o
