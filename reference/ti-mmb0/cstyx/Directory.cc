
#include "Directory.hh"
#include "Fid.hh"
#include "Error.hh"
#include "Client.hh"
using namespace cstyx;

Directory::Directory(Client& c, const std::string& p)
    : _client(c), _list()
{
    {
        Fid f(c,p);
        if (!f.isdir()) throw NotADirectoryError();
        _path=f.path();
    }
    update();
}

Directory::Directory(Client& c)
    : _client(c), _list(), _path(c.cwd())
{
    update();
}

void Directory::update()
{
    std::string b;
    u16 l;
    u32 pos=0;
    Fid f(_client,_path,true,false);
    do {
        l=65535;
        f.read(b,l,pos);
        pos+=l;
    } while (l);
    l=b.size();
    while (b.size()) {
        _list.push_back(Dirent(b,l));
        if (!l) break;
    }
}
