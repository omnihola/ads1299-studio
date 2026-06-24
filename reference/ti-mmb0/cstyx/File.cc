
#include "File.hh"
#include "Client.hh"

using namespace std;
using namespace cstyx;

File::File(Client& client, const std::string& path,
        bool r, bool w)
    : _client(client), _fid(client,path,r,w),
    _pos(0), _eof(false)
{
}

File::~File()
{
}

void File::read(std::string& buf, u16& len)
{
    read(buf,len,_pos);
    _pos+=len;
}

void File::seek(u32 pos)
{
    _pos=pos;
}

int File::readAll(std::string& buf, int lim, u32 pos)
{
    int len=0;
    int got=0;

    if (!lim) return 0;
    seek(pos);
    while (!eof()) {
        len=lim-got;
        if (len<=0) break;
        if (len>1024) len=1024;
		u16 l2=len;
        read(buf,l2);
        got+=l2;
    }
    return got;
}

void File::write(const std::string& buf, u16& len)
{
    write(buf,len,_pos);
    _pos+=len;
}

void File::write(const std::string& buf, u16& len, u32 pos)
{
    _fid.write(buf,len,pos);
    _eof=(len==0);
}

int File::write(const std::string& buf)
{
    u16 len=buf.size();
    if (len>32767) len=32767;
    _fid.write(buf,len,_pos);
    _eof=(len==0);
    return len;
}
