CC = gcc
CXX = g++

FLAGS = -g -D_GNU_SOURCE
CFLAGS += $(FLAGS)
CXXFLAGS += $(FLAGS)

OBJECTS = dxrfd.o

LIBS = -lstdc++ -lrt -lpthread

all: clean build

build: $(OBJECTS)
	$(CC) $(OBJECTS) $(LIBS) $(FLAGS) -o dxrfd

clean:
	rm -f *.o dxrfd
