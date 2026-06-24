
# loads a bin file into the TMS320VC5507/09A.
# call loadDSP(path) to do this.
# basically: we look for the VID & PID, read the bin file into a 
# giant string, open the device, dump the string to endpoint 6,
# ride off into the sunset

import usb
import time,sys

# ids for 5509A USB bootloader
TIVID=0x0451
TIPID=0x9001

def getdevs():
    """Returns a flat array of configs on all busses -- ie an 
    array of tuples of (bus,dev,conf).  Array is guaranteed to be 
    sorted by bus and dev.  Used for giving numbers to configs.
    
    This causes enumerations to happen etc.  Also destroys existing 
    device handles & devices."""
    rtn=[]
    for b in usb.busses():
	for d in b.devices:
	    for c in d.configurations:
		rtn.append((b,d,c))
    return rtn

def findByVendorAndProduct(devlist,vendor,product):
    """Returns a (bus,dev,conf) tuple having the ids vendor and product, 
    or None if none found.  Searches devlist."""
    for t in devlist:
        bus=t[0]
        dev=t[1]
        conf=t[2]
        if dev.idProduct==product and dev.idVendor==vendor:
            return (bus,dev,conf)
    return None

# returns true if vid,pid show up in the usb enum list
def exists(vid,pid):
    if not findByVendorAndProduct(vid,pid) is None:
        return True
    return False

# t is device tuple
def loadDSP(t,binpath):
    # read the file into bin
    #print "binpath",binpath
    f=open(binpath,'rb')
    bin=f.read()
    #print "data length:",len(bin)
    del f
    # find the usb device
    # open & configure the device
    curdev=t
    devh=curdev[1].open()
    devh.setConfiguration(curdev[2])
    devh.claimInterface(curdev[2].interfaces[0][0])
    devh.setAltInterface(0)
    # dump bin to endpoint 6
    # note: need a 5 sec timeout since timeout is for
    # *entire transaction*!!
    # (dude we need to fix libusb)
    devh.bulkWrite(6,bin,5000)
    devh.releaseInterface()
    # now force a device reset.  windoze ddk 
    # recommends this
    #devh.reset()
    # success (we hope)
    # when we return, devh will be deleted
    # we should rescan the bus after this
    return True

# returns:
# 0 = nothing found
# 1 = firmware found -- loading
# 2 = device found
def attemptLoad(binpath,newvid,newpid):
    l=getdevs()
    t=findByVendorAndProduct(l,TIVID,TIPID)
    if t is None:
        t=findByVendorAndProduct(l,newvid,newpid)
        if t is None:
            return 0
        time.sleep(0.5)
        h=t[1].open()
        h.reset()
        time.sleep(0.5)
        return 2
    loadDSP(t,binpath)
    return 1

def printl(s):
    print "\r"+s,
    sys.stdout.flush()

def waitForDevice(binpath,vid,pid):
    r=0
    print
    print("Searching for device ..")
    spin="-\|/"
    spini=0
    while r!=2:
        r=attemptLoad(binpath,vid,pid)
        spinc=spin[spini]
        spini+=1
        if spini>3: spini=0
        if r==0:
            printl(spinc+" nothing found"+" "*40)
        elif r==1:
            i2=3
            while i2:
                printl(spinc+" DSP found; waiting for reload: %d"%i2+" "*30)
                time.sleep(1)
                i2-=1
        elif r==2:
            printl("Board found"+" "*40)
            print
