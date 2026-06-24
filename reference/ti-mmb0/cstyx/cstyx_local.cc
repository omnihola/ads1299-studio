
#include "cstyx_local.h"
#include "LocalConnection.hh"

EXPORT cstyx_connection_t* cstyx_get_local_connection(void)
{
    return (cstyx_connection_t*)(new LocalConnection());
}
