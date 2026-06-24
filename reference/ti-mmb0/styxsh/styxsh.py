#!/usr/bin/python

VERSION="0.1.1"
STYXVENDOR=0x0451
STYXPRODUCT=0x5718

# version 0.1.1 by RO
# changed filehandling so that non-existent files or directories
# do not raise the final big exception, but return to the cmd loop

import sys,StringIO,traceback,hexdump
from cmd2 import Cmd2
from optparse import OptionParser
import pystyx
import dsploader

class Error(Exception): pass

class CmdClient(Cmd2) :
    def __init__(self,*args,**kwargs):
        Cmd2.__init__(self,*args,**kwargs)
        self.conn=None
        self.client=None
        self.prompt='styx> '

    opts_ls=OptionParser(usage="ls [-l]")
    opts_ls.add_option("-l",
              action="store_true", dest="longfmt", default=False,
              help="show detailed listing")

    def do_ls(self, args, opts) :
        if len(args)>1: print "usage: ls [-l] [path]"
        if len(args)==0: p=''
        else: p=args[0]
        d=pystyx.Directory(self.client,p)
        d.ls(opts.longfmt)

    def do_cd(self, args) :
        if len(args) != 1 :
            print "usage: cd path"
            return
        try:
            self.client.cd(args[0])
        except:
            print "path not found, please retry"

    def do_cat(self, args) :
        if len(args) != 1 :
            print "usage: cat path"
            return
        try:
            print self.client.readAll(args[0])
        except:
            print "file not found, please check name"

    opts_hex=OptionParser(usage="hex [-l len] path")
    opts_hex.add_option("-l", "--length",
              type="int", dest="length", default=256,
              help="number of bytes to read (default 256)")

    def do_hex(self,args,opts):
        if len(args) != 1 :
            print "usage: hex [-l len] path"
            return
        s=self.client.readAll(args[0],opts.length)
        print hexdump.dump(s)
        
    def do_get(self, args) :
        if len(args) == 1 :
            f, = args
            f2 = f.split("/")[-1]
        elif len(args) == 2 :
            f,f2 = args
        else :
            print "usage: get path [localname]"
            return
        out = _os(file, f2, "wb")
        self.client.cat(f, out)
        out.close()

    def do_put(self, args) :
        if len(args) == 1 :
            f, = args
            f2 = f.split("/")[-1]
        elif len(args) == 2 :
            f,f2 = args
        else :
            print "usage: put path [remotename]"
            return
        if f == '-' :
            inf = sys.stdin
        else :
            inf = _os(file, f, "rb")
        self.client.put(f2, inf)
        if f != '-' :
            inf.close()

    def do_echo(self,args):
        class err(Exception): pass
        try:
            es=[]
            fn=''
            for i,a in enumerate(args):
                if a[0]=='"':
                    es.append(a[1:-1])
                elif a[0]=='>':
                    if len(a)>1:
                        fn=a[1:]
                    else:
                        try:
                            fn=args[i+1]
                            break
                        except KeyError:
                            raise err
                else:
                    es.append(a)
            es=' '.join(es)
            if fn=='':
                print es
            else:
                try:
                    self.client.write(fn,es)
                except:
                    print "file not found, please check name"
        except err:
            print "usage: echo /stuff/ [>path]"

    def do_connect(self,args):
        if not self.conn is None:
            print "already connected"
            return False
        if len(args)!=1:
            print "usage: connect [usb|local]"
            return
        name=args[0]
        if name=='local':
            self.conn=pystyx.LocalConnection()
        elif name=='usb':
            c=pystyx.USBConnection()
            if not c.search(STYXVENDOR,STYXPRODUCT):
                print "No device found"
                return False
            self.conn=c
        else:
            raise Error("unknown connection name")
        self.client=pystyx.Client(self.conn)

    def do_load5509(self,args):
        dsploader.attemptLoad(args[0],0x451,0x5718)

    def do_quit(self, args):
        return True

    do_q=do_quit
    do_exit=do_quit

    def onecmd(self,line):
        try:
            return Cmd2.onecmd(self,line)
        except Error,e :
            print e.args[0]

def main(prog, *args) :
    import optparse

    parser = optparse.OptionParser(usage="usage: %prog [options] [command]",
                version="%prog"+VERSION)
    parser.add_option("-D", "--debug", dest="debug",
                  help="debugging level")
    #parser.add_option("-V", "--verbose", dest="verbose", action="store_true",
    #            default=False,
    #            help="display lots of protocol information")
    parser.add_option("-X", "--no-readline", dest="readline", action="store_false",
                default=True, help="do not use readline")

    opts,args = parser.parse_args()
    if len(args)>1:
        cmd = args[1:]
    else:
        cmd=''

    cl=CmdClient()
    cl.use_rawinput=opts.readline
    cl.cmdloop("Styx shell v"+VERSION)

if __name__ == "__main__" :
    try :
        main(*sys.argv)
    except KeyboardInterrupt :
        print "interrupted."
    except IOError:
        print "exception"
        traceback.print_exc()
        raw_input()

