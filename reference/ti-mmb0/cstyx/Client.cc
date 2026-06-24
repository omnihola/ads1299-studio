
#include "pathutil.hh"
#include "Error.hh"
#include "Dirent.hh"
#include "File.hh"
#include "styxmsg.hh"
#include "Connection.hh"
#include "Client.hh"
#include <stdlib.h>

using namespace cstyx;
using namespace std;

namespace cstyx {

const u32 rootfid=1;
const u32 cwdfid=2;
const u32 firstfid=10;
const u32 lastfid=32760;

};

/* tmsg

- marshals f into a byte array
- sends f using connection
- receives a new array using connection (may do multiple times)
- unmarshals connection into f (reuses f)
- throws StyxError with message if it's an error
*/
static void tmsg(Connection& c, fcall_t& f)
{
    u32 l=cstyx_sizeS2M(&f);
    std::string buf;
    
    cstyx_convS2M(&f,buf);
    c.tx(buf,l);
    l=4;
    c.rx(f.data,l);
    l=(f.data[0]&0xff)|(f.data[1]<<8);
    if (l<4) throw Error("bad message size");
    u32 l2=l-4;
    c.rx(f.data,l2);
    c.flushInput();
    l2=cstyx_convM2S(const_cast<char*>(f.data.data()),l,&f);
    if (!l2) throw Error("message corrupt");
    if (f.type==Rerror) throw Error(f.u.ename);
}

void Client::tattach(u32 fid)
{
	fcall_t f;

	f.type=Tattach;
	f.fid=fid;
	f.tag=getTag();
	f.u.attach.afid=0xffffffff;
	f.u.attach.uname="nobody";
	f.u.attach.aname="nobody";
	tmsg(_conn,f);
}

void Client::twalk(u32 fid, u32 newfid, const std::vector<std::string>& path)
{
	fcall_t f;

	f.type=Twalk;
	f.fid=fid;
	f.u.twalk.newfid=newfid;
	f.u.twalk.nwname=path.size();
	int i=0;
	vector<string>::const_iterator ii;
	for (ii=path.begin();ii!=path.end();++ii)
		f.u.twalk.wname[i++]=const_cast<char*>(ii->c_str());
	tmsg(_conn,f);
}

void Client::topen(u32 fid, u32 mode)
{
	fcall_t f;

	f.type=Topen;
	f.fid=fid;
	f.u.create.mode=mode;
	tmsg(_conn,f);
}

void Client::tclunk(u32 fid)
{
	fcall_t f;

	f.type=Tclunk;
	f.fid=fid;
	tmsg(_conn,f);
}

Dirent Client::tstat(u32 fid)
{
	fcall_t f;

	f.type=Tstat;
	f.fid=fid;
	tmsg(_conn,f);
	return Dirent(f);
}

void Client::tread(u32 fid, std::string& buf, u16& len, u32 pos)
{
	fcall_t f;
	
	f.type=Tread;
	f.fid=fid;
	f.u.rw.offset=pos;
	f.u.rw.count=len;
	tmsg(_conn,f);
	len=(u16)f.u.rw.count;
	if (len) {
		buf.append(const_cast<const char*>(f.u.rw.data),len);
	}
}

void Client::twrite(u32 fid, const std::string& buf, u16& len, u32 pos)
{
	fcall_t f;
	
	f.type=Twrite;
	f.fid=fid;
	f.u.rw.offset=pos;
	f.u.rw.count=len;
	f.u.rw.data=const_cast<char*>(buf.data());
	tmsg(_conn,f);
	len=(u16)f.u.rw.count;
}

Client::Client(Connection& c)
	: _conn(c), _nextfid(firstfid), _connected(false),
        _root(*this,"/",1), _cwd(*this,"/",2)
{
    connect();
}

u32 Client::getFid()
{
	int f=_nextfid++;
	if (_nextfid>lastfid) _nextfid=firstfid;
	return f;
}

void Client::twalk(u32 fid, u32 newfid)
{
    vector<string> pl;
    twalk(fid,newfid,pl);
}

void Client::connect()
{
    if (connected()) return;
    try {
        tattach(_root.fid());
        twalk(_root.fid(),_cwd.fid());
    } catch (Error) {
        throw CannotConnectError();
    }
    _connected=true;
}

void Client::setCWD(const std::string& p)
{
    std::string old=_cwd.path();
    _cwd.walk(p);
    if (!_cwd.isdir()) {
        _cwd.walk(old);
        throw Error("not a directory");
    }
}

bool Client::isdir(const std::string &p)
{
    try {
        Fid f(*this,p);
        return f.isdir();
    } catch (...) {
    }
    return false;
}

bool Client::exists(const std::string &p)
{
    try {
        Fid f(*this,p);
    } catch (...) {
        return false;
    }
    return true;
}

int Client::write(const std::string& path, const std::string& data)
{
    File f(*this,path);
    return f.write(data);
}

int Client::readAll(const std::string& path, std::string& buf, int lim)
{
    File f(*this,path);
    return f.readAll(buf,lim);
}
