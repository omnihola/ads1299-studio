
#include "cstyx_usb.h"
#include "USBConnection.hh"

EXPORT cstyx_usb_connection_t* cstyx_get_usb_connection(void)
{
    try {
        return (cstyx_usb_connection_t*)(new USBConnection());
    } catch (...) {
    }
    return 0;
}

EXPORT int cstyx_usb_search(cstyx_usb_connection_t* c, 
        unsigned short vid, unsigned short pid)
{
    if (!c) return -1;
    try {
        USBConnection* u=(USBConnection*)(c);
        return (u->search(vid,pid)?1:0);
    } catch (...) {
    }
    return -1;
}

EXPORT void cstyx_delete_usb_connection(cstyx_usb_connection_t* c)
{
    try {
        delete (USBConnection*)c;
    } catch (...) {
    }
}
