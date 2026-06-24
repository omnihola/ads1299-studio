
#include "Error.hh"

using namespace cstyx;

static Error latestError;

Error::Error(const std::string& msgstr, int code)
        : _msg(msgstr), _code(code)
{
    latestError=*this;
}

Error::Error()
    : _msg("no error"), _code(0)
{
    latestError=*this;
}

Error::Error(int code)
    : _msg("coded error"), _code(code)
{
    latestError=*this;
}

const Error& Error::getLatestError()
{
    return latestError;
}
