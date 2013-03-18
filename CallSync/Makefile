#
# Copyright (C) 2013   Artem Prilutskiy, R3ABM (r3abm@dstar.su)
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

CC = gcc
CXX = g++

FLAGS = -g -rdynamic -O3 $(shell pkg-config --cflags sqlite3 libconfig)
CFLAGS += $(FLAGS)
CXXFLAGS += $(FLAGS)

OBJECTS = \
    StringTools.o DStarTools.o \
    MySQLStore.o StoreFactory.o DDBClient.o \
    ReportTools.o CallSync.o

LIBS = -lstdc++ -lbsd -lircclient -lmysqlclient $(shell pkg-config --libs sqlite3 libconfig)

all: clean build

build: $(OBJECTS)
	$(CC) $(OBJECTS) $(LIBS) $(FLAGS) -o callsync

clean:
	rm -f *.o callsync
