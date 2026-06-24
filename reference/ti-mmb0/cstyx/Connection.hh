
#ifndef GUARD_connection_hh
#define GUARD_connection_hh

#include "types.h"
#include <string>

namespace cstyx {

//! Stream connection
/*! Connection objects are used by Clients to communicate with Styx servers.

Connection is an abstract base.  The cstyx library does not include any real
Connection subclasses.  These are available in other libraries, such as
cstyx_local and cstyx_usb.

Connection subclasses must establish the connection at construction time, 
and disconnect at destruction time.  If the connection cannot be
established, the subclass must throw CannotConnectError.

It may be possible for a Connection to lose connectivity.  If this
happens, isConnected() must return false, and rx() and tx() must throw
NotConnectedError.

A Connection cannot be re-established.  If a Connection loses connectivity, 
it must be discarded and a new Connection obtained.  

Connections are not copyable.

Connection libraries and Connection subclasses may include additional
methods for obtaining a Connection. Because of the wide range of possible
connections and ways of obtaining them, these methods cannot practically be
standardised.
*/
class Connection {
private:
    Connection(const Connection&);
    const Connection& operator=(const Connection&);

protected:
    Connection() {}

public:
    virtual ~Connection() {}

    virtual const std::string& name() const=0;

    virtual bool isConnected() const=0;

    virtual void rx(std::string& buf, u32& len)=0;

    virtual void tx(const std::string& buf, u32& len)=0;

    virtual void flushInput()=0;
};

};

#endif
