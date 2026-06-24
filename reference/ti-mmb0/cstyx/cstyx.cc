
#include "cstyx.h"
#include "cstyx.hh"
#include <vector>
#include <string>
#include "Directory.hh"

using namespace cstyx;
using namespace std;

EXPORT void cstyx_close_connection(cstyx_connection_t* c)
{
    if (!c) return;
    try {
        delete (Connection*)c;
    } catch(...) {
    }
}

EXPORT void cstyx_close_client(cstyx_client_t* c)
{
    if (!c) return;
    try {
        delete (Client*)c;
    } catch(...) {
    }
}

EXPORT cstyx_dir_t* cstyx_read_dir(cstyx_client_t* c, const char* p)
{
    if (!c) return 0;
    Directory* d;
    try {
        if (p)
            d=new Directory(*(Client*)c,p);
        else
            d=new Directory(*(Client*)c);
    } catch (...) {
        return 0;
    }
    return (cstyx_dir_t*)d;
}

EXPORT int cstyx_path_isdir(cstyx_client_t* c, const char* p)
{
    if (!c) return 0;
    return ((Client*)c)->isdir(p);
}

EXPORT int cstyx_dirent_isdir(cstyx_dirent_t* e)
{
    if (!e) return 0;
    return ((Dirent*)e)->isdir();
}

EXPORT int cstyx_get_dir_length(cstyx_dir_t* d)
{
    if (!d) return 0;
    try {
        return ((Directory*)d)->list().size();
    } catch (...) {
    }
    return 0;
}

EXPORT const char* cstyx_get_dir_path(cstyx_dir_t* d)
{
    if (!d) return 0;
    try {
        return ((Directory*)d)->path().c_str();
    } catch (...) {
    }
    return 0;
}

EXPORT cstyx_dirent_t* cstyx_get_dirent(cstyx_dir_t* d, int i)
{
    if (!d) return 0;
    try {
        return (cstyx_dirent_t*)(new Dirent( ((Directory*)d)->list()[i] ) );
    } catch (...) {
    }
    return 0;
}

//! Finish reading a directory
EXPORT void cstyx_close_dir(cstyx_dir_t* d)
{
    if (!d) return;
    delete (Directory*)d;
}

EXPORT long cstyx_get_dirent_mode(cstyx_dirent_t* e)
{
    if (!e) return 0;
    return ((Dirent*)e)->mode();
}

EXPORT const char* cstyx_get_dirent_name(cstyx_dirent_t* e)
{
    if (!e) return 0;
    return ((Dirent*)e)->name().c_str();
}

EXPORT u32 cstyx_get_dirent_len(cstyx_dirent_t* e)
{
    if (!e) return 0;
    return ((Dirent*)e)->len();
}

EXPORT const char* cstyx_get_cwd(cstyx_client_t* d)
{
    if (!d) return 0;
    return ((Client*)d)->cwd().c_str();
}

EXPORT int cstyx_set_cwd(cstyx_client_t* d, const char* path)
{
    if (!d) return -1;
    if (!path) return 0;
    Client* c=(Client*)d;
    try {
        c->setCWD(path);
    } catch (...) {
        return -1;
    }
    return 0;
}

EXPORT cstyx_file_t* cstyx_open(cstyx_client_t* d, const char* path, int mode)
{
    if (!d) return 0;
    if (!path) return 0;
    try {
        bool r,w;
        r=mode&CSTYX_READ?true:false;
        w=mode&CSTYX_WRITE?true:false;
        return (cstyx_file_t*)(new File(*(Client*)d,path,r,w));
    } catch (...) {
    }
    return 0;
}

EXPORT cstyx_dirent_t* cstyx_stat(cstyx_file_t* f)
{
    if (!f) return 0;
    try {
        return (cstyx_dirent_t*)(new Dirent( ((File*)f)->stat() ));
    } catch (...) {
    }
    return 0;
}

EXPORT void cstyx_delete_dirent(cstyx_dirent_t* d)
{
    if (!d) return;
    delete (Dirent*)d;
}

EXPORT long cstyx_get_file_len(cstyx_file_t* f)
{
    if (!f) return -1;
    try {
        return ((File*)f)->size();
    } catch (...) {
    }
    return -1;
}

EXPORT void cstyx_close(cstyx_file_t* f)
{
    if (!f) return;
    delete (File*)f;
}

EXPORT int cstyx_read(cstyx_file_t* f, char* d, u16* len)
{
    if (!f||!d||!len) return -1;
    string buf;
    try {
        ((File*)f)->read(buf,*len);
    } catch(...) {
        return -1;
    }
    memcpy(d,buf.data(),buf.size());
    return 0;
}

EXPORT int cstyx_read_all_path(cstyx_client_t* c, const char* path, char* buf, int lim)
{
    if (!c||!path||!buf) return -1;
    try {
        std::string str;
        ((Client*)c)->readAll(path,str,lim);
        memcpy((void*)buf,str.data(),str.size());
        return str.size();
    } catch(...) {
    }
    return -1;
}

EXPORT int cstyx_write(cstyx_file_t* f, const char* d, u16* len)
{
    if (!f||!d||!len) return -1;
    string buf(d);
    try {
        ((File*)f)->write(d,*len);
    } catch (...) {
        return -1;
    }
    return 0;
}

EXPORT int cstyx_write_path(cstyx_client_t* c, const char* path, const char* d, int len)
{
    if (!c||!path||!d) return -1;
    try {
        std::string s(d,len);
        return ((Client*)c)->write(path,s);
    } catch(...) {
    }
    return -1;
}

EXPORT cstyx_client_t* cstyx_connect(cstyx_connection_t* c)
{
    if (!c) return 0;
    try {
        return (cstyx_client_t*)(new Client(*(Connection*)c));
    } catch (...) {
    }
    return 0;
}

EXPORT const char* cstyx_get_latest_error(int* d)
{
    const Error& e=Error::getLatestError();
    if (d) *d=e.code();
    return e.msg().c_str();
}
