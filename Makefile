CC=clang++
OPTS=-std=c++14 -Wall -Werror
EXE=main
OBJ=# add *.o files here

all: $(EXE)

$(EXE): main.cc $(OBJ)
	$(CC) $(OPTS) -o $(EXE) main.cc $(OBJ)

# Copy for each *.o file
# example.o: src/example.cc
# 	$(CC) $(OPTS) -c src/example.cc

clean:
	rm -rf $(EXE) *.o
