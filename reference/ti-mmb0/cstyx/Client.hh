
#ifndef GUARD_cstyx_Client_hh
#define GUARD_cstyx_Client_hh

#include "Dirent.hh"
#include "Fid.hh"
#include <string>
#include <vector>
#include "Error.hh"

namespace cstyx {

class Connection;

class Client {
    friend class File;
    friend class Fid;
private:
    Connection& _conn;
    int _nextfid;
    bool _connected;
    Fid _root;
    Fid _cwd;

    // get a new fid -- number only
    u32 getFid();

    u16 getTag() { return 1; }

    //const Fid& getRootFid() const { return _root; }

    /* sends Tattach.  \p fid is the root fid to use.  This does not 
    authenticate; it sends -1 for afid and "nobody" for the auth user */
    void tattach(u32 fid);

    /* basic twalk.  fid and newfid are as described in the protocol docs.  
    if fid is attached to a file, \p path is ignored and \p fid is copied 
    to \p newfid (if you want to do this you can also use twalk(fid,newfid) )
    this also happens if path is empty (even if \p fid is a directory).
    if \p fid is a directory then a walk is done for each element of \p path
    and \p newfid gets the result.  \p newfid can be the same as \p fid btw.
    
    this fn knows nothing about the cwd, relative paths, etc. etc.  see walk() 
    for higher-level ops.
    */
    void twalk(u32 fid, u32 newfid, const std::vector<std::string>& path);

    /* makes \p newfid point at the same thing as \p fid.  like calling 
    twalk(fid,newfid,path) with an empty \p path. */
    void twalk(u32 fid, u32 newfid); // copies fid to newfid

    /* this is a slightly higher-level twalk.  it knows about cwd and relative 
    paths as follows: 1) if \p path is relative, the walk is done from cwd. 
    2) if \p path is absolute, the walk is done from the root.
    in both cases: \p newfid is the new fid to use, and \p newpath gets set 
    to the fully normalised absolute path which resulted from the walk.
    
    this is mostly called by walk() */
    void twalk(const std::string& path, u32 newfid, std::string& newpath);

    /* open \p fid with \p mode */
    void topen(u32 fid, u32 mode);
    /* clunk \p fid */
    void tclunk(u32 fid);
    /* stat \p fid.  makes a new Dirent() */
    Dirent tstat(u32 fid);
    /* read from \p fid.  will (should) throw an exc if \p fid is not open */
    void tread(u32 fid, std::string& buf, u16& len, u32 pos=0);
    /* write to \p fid */
    void twrite(u32 fid, const std::string& buf, u16& len, u32 pos=0);

    /* this walks path using either cwdfid or rootfid
    depending on if path is relative or not.
    returns the new fid and complete normalised path.
    gets new fid from getFid(). will try again if fid is in use.
    calls twalk and may throw any of its exceptions */
    void walk(const std::string& path, u32& newfid, std::string& newpath);

    /* does Tattach, then copies rootfid to cwdfid.
    this is called at construction time */
    void connect();

public:
    Client(Connection&);

    //! Find out whether the client is connected
    bool connected() const { return _connected; }

    //! Get the current working directory
    const std::string& cwd() const
    {
        if (!connected()) throw NotConnectedError();
        return _cwd.path();
    }

    //! Set the current working directory
    void setCWD(const std::string&);
    //! List the given directory
    void ls(const std::string&, std::vector<Dirent>&);
    //! List the current working directory
    void ls(std::vector<Dirent>&);

    bool exists(const std::string&);
    bool isdir(const std::string&);
    bool isfile(const std::string& p) { return !isdir(p); }

    int write(const std::string& path, const std::string& data);
    int readAll(const std::string& path, std::string& buf, int lim=32767);
};

};

#endif
