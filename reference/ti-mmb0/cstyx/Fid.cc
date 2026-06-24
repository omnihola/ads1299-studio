
#include "Fid.hh"
#include "Client.hh"
#include "Error.hh"

using namespace cstyx;

void Fid::walk(const std::string& p, u32 fid, std::string& newpath)
{
    u32 walkfid;
    
    if (!p.size()) return;
    walkfid=(p[0]=='/')?_client._root.fid():_client._cwd.fid();
    std::vector<std::string> pl;
    pathutil::split(p,pl);
    _client.twalk(walkfid,fid,pl);
    if (p[0]!='/')
        newpath=_client._cwd.path()+"/"+p;
    pathutil::normalise(newpath);
}

void Fid::walk(const std::string& s)
{
    for (int i=0;i<10;++i) {
        try {
            walk(s,_fid,_path);
            return;
        } catch (Error) {
            continue;
        }
        break;
    }
    throw Error("cannot get a fid");
}

Fid::Fid(const Fid& f)
    : _client(f._client), _path(f._path), _open(false)
{
    for (int i=0;i<10;++i) {
        try {
            _fid=_client.getFid();
            _client.twalk(f._fid,_fid);
            return;
        } catch (Error) {
            continue;
        }
        break;
    }
    throw Error("cannot get a fid");
}

Fid::Fid(Client& c, const std::string& s)
    : _client(c), _fid(c.getFid()), _path(s), _open(false)
{
    if (_path.size()==0) _path=c.cwd();
    walk(_path);
}

// private constructor
Fid::Fid(Client& c, const std::string& s, u32 f)
    : _client(c), _fid(f), _path(s), _open(false)
{
}

Fid::Fid(Client& c, const std::string& s, bool r, bool w)
    : _client(c), _fid(c.getFid()), _path(s), _open(false)
{
    walk(s);
    open(r,w);
}

Fid::~Fid()
{
    try {
        _client.tclunk(_fid);
    } catch(...) {
    }
}

Fid& Fid::operator=(const Fid& f)
{
    if (this!=&f) {
        _path=f._path;
        _client.twalk(f._fid,_fid);
    }
    return *this;
}

void Fid::open(bool r, bool w)
{
    u32 mode=0;
    
    if (r) mode|=0444;
    if (w) mode|=0222;
    _client.topen(_fid,mode);
    _open=true;
}

void Fid::read(std::string& buf, u16& len, u32 pos)
{
    _client.tread(_fid,buf,len,pos);
}

void Fid::write(const std::string& buf, u16& len, u32 pos)
{
    _client.twrite(_fid,buf,len,pos);
}

Dirent Fid::stat() const
{
    return _client.tstat(_fid);
}
