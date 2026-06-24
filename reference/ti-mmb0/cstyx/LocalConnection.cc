
#include "LocalConnection.hh"

extern "C" {
#include "estyx/estest/estest.h"
}

using namespace cstyx;
using namespace std;

const string LocalConnection::_name="local";

// **** EVIL EVIL EVIL ****
static LocalConnection* theGlobalObject;
// **** EVIL EVIL EVIL ****

LocalConnection::LocalConnection()
    : _rxq(), _srv(0)
{
    _srv=estest_init(::receiveFromServer);
    if (!_srv) throw CannotConnectError();
    theGlobalObject=this;
}

int LocalConnection::receiveFromServer(estyx_server_t *server,
    u16 len, u8* buf, 
    u16 datalen, u8* data)
{
    theGlobalObject->_rxq.append((char*)buf,len);
    if (datalen) theGlobalObject->_rxq.append((char*)data,datalen);
    return 0;
}

int receiveFromServer(estyx_server_t *server,
    u16 len, u8* buf, 
    u16 datalen, u8* data)
{
    LocalConnection::receiveFromServer(server,len,buf,datalen,data);
    return 0;
}

void LocalConnection::rx(std::string& buf, u32& len)
{
    if (len>_rxq.size()) len=_rxq.size();
    if (!len) return;
    buf.append(_rxq,0,len);
    _rxq.erase(0,len);
}

void LocalConnection::tx(const std::string& buf, u32& len)
{
    estyx_rx(_srv,len,(u8*)buf.data());
}
