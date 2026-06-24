
# Use this with MSYS / mingw32

CC=mingw32-gcc
CXX=mingw32-g++
pyrex=pyrexc
pyinc=/usr/include/python2.4
CPPFLAGS=-Ilocal -I$(pyinc)
CCFLAGS=-I$(pyinc)

cstyx_sources=\
	../Client.cc \
	../cstyx.cc \
	../cstyx_local.cc \
	../cstyx_usb.cc \
	../Directory.cc \
	../Dirent.cc \
	../Error.cc \
	../Fid.cc \
	../File.cc \
	../LocalConnection.cc \
	../pathutil.cc \
	../styxmsg.cc \
	../USBConnection.cc

cstyx_headers=\
	../Client.hh \
	../Connection.hh \
	../cstyx.h \
	../cstyx_local.h \
	../cstyx_usb.h \
	../Directory.hh \
	../Dirent.hh \
	../Error.hh \
	../Fid.hh \
	../File.hh \
	../LocalConnection.hh \
	../pathutil.hh \
	../poppack.h \
	../pshpack1.h \
	../styxmsg.hh \
	../types.h \
	../usb.h \
	../USBConnection.hh

../estyx/estest/estest.a:
	make -C ../estyx/estest estest.a

cstyx.dll: $(cstyx_headers) $(cstyx_sources) ../estyx/estest/estest.a
	$(CXX) -shared -o cstyx.dll $(cstyx_sources) libusb.a ../estyx/estest/estest.a

pystyx.c: pystyx.pyx
	$(pyrex) pystyx.pyx

objs=cstyx.o cstyx_c.o pathutil.o pystyx.o \
	RegisterConnections.o LocalConnection.o

pystyx.dll: $(objs) local/estest.a
	$(CXX) -g -shared $(objs) -L/usr/lib/python2.4/config local/estest.a -lpython2.4 -o pystyx.dll


clean:
	- rm -f $(objs) pystyx.dll
