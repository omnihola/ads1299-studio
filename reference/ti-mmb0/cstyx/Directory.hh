
#ifndef GUARD_cstyx_Directory_hh
#define GUARD_cstyx_Directory_hh

#include <vector>
#include <string>
#include "Dirent.hh"

namespace cstyx {

class Directory {
public:
    typedef std::vector<Dirent> List;
    typedef List::const_iterator iterator;

private:
    Client& _client;
    std::string _path;
    List _list;

public:

    Directory(Client& c, const std::string& p);
    Directory(Client& c);

    const List& list() const { return _list; }
    void update();
    const std::string& path() const { return _path; }
};

};

#endif
