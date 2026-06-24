
#include "Dirent.hh"
#include "Error.hh"
#include "styxmsg.hh"

using namespace cstyx;

u16 Dirent::initFromBuf(std::string& buf)
{
    dir_t d;
    u16 l;
    char c[512];

    l=cstyx_convM2D(const_cast<char*>(buf.data()),buf.size(),&d,c);
    if (!l) throw Error("bad message");
    
    _name=d.name;
    _len=d.length;
    _mode=d.mode;
    
    buf.erase(0,l);
    return l;
}

Dirent::Dirent(fcall_t& f)
{
    std::string buf(f.u.stat.stat,f.u.stat.nstat);
    initFromBuf(buf);
}
