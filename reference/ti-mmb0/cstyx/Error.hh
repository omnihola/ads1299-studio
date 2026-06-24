
#ifndef GUARD_CStyxErrors_hh
#define GUARD_CStyxErrors_hh

#include <string>

namespace cstyx {

class Error {
private:
    std::string _msg;
    int _code;
public:
    Error(const std::string& msgstr, int code=-1);
    Error();
    Error(int code);

    const std::string& msg() const { return _msg; }
    int code() const { return _code; }
    
    static const Error& getLatestError();
};

struct NotOpenError : public Error
{
    NotOpenError() : Error("not open") {}
};

struct CannotConnectError : public Error
{
    CannotConnectError() : Error("cannot connect") {}
};

struct NotConnectedError : public Error
{
    NotConnectedError() : Error("not connected") {}
};

struct NotADirectoryError : public Error
{
    NotADirectoryError() : Error("not a directory") {}
};

#if 0
class FidInUseError : public Error {};
class PathNotFoundError : public Error {};
class NotConnectedError : public Error {};
class PermissionDeniedError : public Error {};
class BadMessageSizeError : public Error {};
class BadMessageError : public Error {};
class NotADirectoryError : public Error {};
class CannotOpenError : public Error {};
class NoSuchDeviceError : public Error {};
class TimeoutError : public Error {};
#endif

};

#endif
