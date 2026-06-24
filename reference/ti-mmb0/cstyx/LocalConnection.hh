
#ifndef GUARD_LocalConnection_hh
#define GUARD_LocalConnection_hh

#include "cstyx.hh"
#include "Connection.hh"
#include <string>

typedef struct estyx_server_t estyx_server_t;

extern "C"
int receiveFromServer(estyx_server_t* server, u16 len, u8* buf, 
        u16 datalen, u8* data);

class LocalConnection : public cstyx::Connection {
    friend int receiveFromServer(estyx_server_t* server, u16 len, u8* buf, 
        u16 datalen, u8* data);
private:
    static const std::string _name;
    std::string _rxq;
    estyx_server_t* _srv;

    static int receiveFromServer(estyx_server_t* server, u16 len,
        u8* buf, 
        u16 datalen, u8* data);

public:
    LocalConnection();

    virtual const std::string& name() const { return _name; }
    virtual bool isConnected() const { return true; }
    virtual void rx(std::string& buf, u32& len);
    virtual void tx(const std::string& buf, u32& len);
    virtual void flushInput() {}
};

#endif
