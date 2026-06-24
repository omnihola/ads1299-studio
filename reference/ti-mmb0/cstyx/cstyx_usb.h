
#ifndef GUARD_cstyx_usb_h
#define GUARD_cstyx_usb_h

#include "cstyx.h"

typedef struct cstyx_usb_connection_t cstyx_usb_connection_t;

#ifdef __cplusplus
extern "C" {
#endif

//! Obtain a USB connection object
/*! Creates a USBConnection object.

You must call cstyx_usb_search() to associate the object with a USB device.
If you do not do this, calls to functions which cause communication will
throw cstyx::NotConnectedError().

If an error occurs, this function returns null.

*/
EXPORT cstyx_usb_connection_t* cstyx_get_usb_connection(void);

//! Search a USB connection object
/*! Calls USBConnection::search() to search for a USB device to use.

Unlike USBConnection::search(), this function returns an integer result. If
a device is found, this function returns 1; if no device is found, this
function returns 0. If an error occurs, this function returns -1.

See USBConnection::search() for details about the search itself.

*/
EXPORT int cstyx_usb_search(cstyx_usb_connection_t* c,
	unsigned short vid, unsigned short pid);

EXPORT void cstyx_delete_usb_connection(cstyx_usb_connection_t* c);

#ifdef __cplusplus
}
#endif

#endif
