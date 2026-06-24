
#include "USBConnection.hh"
#include "usb.h"
#include "Error.hh"

using namespace cstyx;

bool USBConnection::_init=false;

USBConnection::USBConnection()
    : _devh(0), _dev(0), _name("none"), _iface(-1), _inep(0), _outep(0),
    _timeout(3000)
{
    if (!_init) {
        usb_init();
        usb_set_debug(0);
    }
}

USBConnection::~USBConnection()
{
    if (_devh) {
        usb_release_interface(_devh,_iface);
        usb_close(_devh);
    }
}

bool USBConnection::search(u16 vid, u16 pid)
{
    if (_devh) return true;

    usb_find_busses();
    usb_find_devices();
    
    for (usb_bus* bus = usb_get_busses(); bus; bus = bus->next) {
        struct usb_device* dev;
            
        for (dev = bus->devices; dev; dev = dev->next) {
            struct usb_device_descriptor* desc=&(dev->descriptor);
            if (vid==desc->idVendor&&pid==desc->idProduct) {
                /* for now, we only use the first config, iface and alt setting. 
                Later we should search on descriptor strings etc. */
                if (!desc->bNumConfigurations) continue;
                _devh=usb_open(dev);
                if (!_devh) continue;
                _dev=dev;
                struct usb_config_descriptor* conf=dev->config;
                usb_set_configuration(_devh,conf->bConfigurationValue);
                struct usb_interface_descriptor* iface=conf->interface->altsetting;
                _iface=iface->bInterfaceNumber;
                if (usb_claim_interface(_devh,_iface)||
                        usb_set_altinterface(_devh,iface->bAlternateSetting)) {
                    usb_close(_devh);
                    _dev=0;
                    _devh=0;
                    continue;
                }
                _name="USB";
                _pktsize=iface->endpoint->wMaxPacketSize;
                _inep=0x81;
                _outep=1;
                usb_clear_halt(_devh,_inep);
                usb_clear_halt(_devh,_outep);
                return true;
            }
        }
    }
    
    return isConnected();
}

// returns l rounded to the next highest multiple of _pktsize
u32 USBConnection::adjustTxnSize(u32 l)
{
    u32 l2=l/(u32)_pktsize;
    l2+=1;
    return l2*_pktsize;
}

void USBConnection::rxUSB(std::string& buf, u32& len)
{
    if (!isConnected()) throw NotConnectedError();
    u32 actlen=adjustTxnSize(len);
    char* s=new char[actlen];
    if (usb_bulk_read(_devh,_inep,s,actlen,_timeout)<0) throw Error("usb receive error");
    buf.append(s,actlen);
    delete[] s;
}

void USBConnection::rx(std::string& buf, u32& len)
{
    u32 got=0;
    u32 l2;

    if (!isConnected()) throw NotConnectedError();
    if (_rxq.size()<len) {
        l2=len-_rxq.size();
        rxUSB(_rxq,l2);
    }
    buf.append(_rxq,0,len);
    _rxq.erase(0,len);
}

void USBConnection::flushInput()
{
    _rxq.clear();
}

void USBConnection::tx(const std::string& buf, u32& len)
{
    u32 actlen;
    std::string buf2;

    if (!isConnected()) throw NotConnectedError();
    actlen=adjustTxnSize(len);
    buf2=buf;
    buf2.append(actlen-len,(char)1);
    if (usb_bulk_write(_devh,_outep,(char*)buf2.data(),actlen,_timeout)<0)
        throw Error("usb transmit error");
}
