
#ifndef GUARD_USBConnection_hh
#define GUARD_USBConnection_hh

#include "cstyx.hh"
#include "Connection.hh"
#include <string>

struct usb_device;
struct usb_dev_handle;

class USBConnection : public cstyx::Connection {
private:
    static bool _init;
    usb_dev_handle* _devh;
    usb_device* _dev;
    int _iface;
    int _pktsize;
    unsigned char _inep, _outep;
    int _timeout;

    std::string _name;
    std::string _rxq;

    u32 adjustTxnSize(u32 l);
    void rxUSB(std::string& buf, u32& len);

public:
    //! Create a USB connection object
    /*!

    \warning Do not create more than one USBConnection object at a time.
    Attempts to use more than one USBConnection at a time may cause the process
    to crash. (This restriction is partly due to a flaw in the design of
    libusb.)

    */
    USBConnection();
    virtual ~USBConnection();

    //! Search for a USB device
    /*! Searches the system's list of available (i.e., connected and enumerated)
    USB devices for one having vendor ID \p vid and product ID \p pid, and
    claims the first one found. After a successful call to this function,
    the connection object can be used with a Client.
    
    If a device is found, this function returns true. If no device is found,
    this function returns false.  If an error occurs, this function throws 
    an exception.
    
    After this function returns true, subsequent calls will return true
    without searching the USB system. To search the USB system again, you
    must use a new USBConnection object.
    
    \warning Delete the old object before creating a new one.  See the 
    constructor documentation for details.

    */
    bool search(u16 vid, u16 pid);

    virtual const std::string& name() const { return _name; }
    virtual bool isConnected() const { return _devh!=0; }
    virtual void rx(std::string& buf, u32& len);
    virtual void tx(const std::string& buf, u32& len);
    virtual void flushInput();
};

#endif
