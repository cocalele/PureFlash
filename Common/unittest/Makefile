#
#   A wrapper for C++ sockets on Unix and Windows
#   Copyright (C) 2002
#
#   This program is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation; either version 2 of the License, or
#   (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program; if not, write to the Free Software
#   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#

CXX = g++
CXXFLAGS = -Wall -ansi -pedantic -Wno-variadic-macros -g -I../include -DDEBUG -lpthread
ifeq ($(shell uname),SunOS)
  LIBS = -lsocket -lnsl
endif

all: clt_socket srv_socket

clt_socket: test_clt_socket.cpp ../src/s5socket.cpp ../src/s5message.cpp  ../include/s5socket.h
	$(CXX) $(CXXFLAGS) -o clt_socket ../src/s5socket.cpp ../src/s5message.cpp test_clt_socket.cpp $(LIBS)

srv_socket: test_srv_socket.cpp ../src/s5socket.cpp ../src/s5message.cpp ../include/s5socket.h
	$(CXX) $(CXXFLAGS) -o srv_socket ../src/s5socket.cpp ../src/s5message.cpp test_srv_socket.cpp $(LIBS)

clean:
	$(RM) clt_socket  srv_socket