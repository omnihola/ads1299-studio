
CC=mingw32-gcc
CXX=mingw32-g++
pyrex=pyrexc
#pyinc=/usr/include/python2.4
pyinc=/c/Python24/include
CPPFLAGS=-Ilocal -I$(pyinc)
CCFLAGS=-I$(pyinc) -Lc:\\Python24\\libs\\

pystyx.c: pystyx.pyx
	$(pyrex) pystyx.pyx

objs=cstyx.o cstyx_c.o pathutil.o pystyx.o \
	RegisterConnections.o LocalConnection.o

pystyx.dll: $(objs) local/estest.a
	$(CXX) -g -shared $(objs) -Lc:/Python24/libs/ -L/usr/lib/python2.4/config local/estest.a -lpython24 -o pystyx.dll

local/estest.a:
	$(MAKE) -c local estest.a

clean:
	- rm -f $(objs) pystyx.dll