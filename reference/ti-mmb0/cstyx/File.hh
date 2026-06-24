
#ifndef GUARD_cstyx_File_hh
#define GUARD_cstyx_File_hh

#include "Fid.hh"
#include "Dirent.hh"
#include <string>

namespace cstyx {

class Client;

class File {
    friend class Client;
private:
    Client& _client;
    Fid _fid;
    long _pos;
    bool _eof;

public:
    File(Client& client, const std::string& path,
        bool r=true, bool w=false);
    ~File();

    Dirent stat() const { return _fid.stat(); }
    std::string name() const { return _fid.name(); }
    const std::string& path() const { return _fid.path(); }
    long size() const { return stat().size(); }
    void read(std::string& buf, u16& len);
    void read(std::string& buf, u16& len, u32 pos) { _fid.read(buf,len,pos); _eof=(len==0); }
    void seek(u32 pos);
    u32 pos() const { return _pos; }
    int readAll(std::string& buf, int lim=65535, u32 pos=0);
    //! Write an entire string
    int write(const std::string& buf);
    //! Write part of a string
    void write(const std::string& buf, u16& len);
    //! Write part of a string to a file position
    void write(const std::string& buf, u16& len, u32 pos);
    bool eof() const { return _eof; }
    bool isdir() { return stat().isdir(); }
    u32 fid() const { return _fid.fid(); }
};

};

#endif
