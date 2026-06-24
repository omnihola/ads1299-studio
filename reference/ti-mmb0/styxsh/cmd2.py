
from cmd import Cmd
import shlex
from optparse import OptionParser

class Cmd2(Cmd):
    """Extension of the standard Cmd module.
    
    New features:
    
    - do_* commands receive a list of arguments parsed by shlex in 
      compatibility mode.  See the shlex documentation for parsing 
      rules.
      
    - Commands can have an associated OptionParser object from 
      the optparse module, named opts_<cmd> where <cmd> is the command 
      name.  If an OptionParser object is present, Cmd2 uses it as
      follows:
	  
      * Cmd2 calls the OptionParser's parse_args method before calling the 
      command's do_* method.  The object containing the parsed values is 
      passed to the do_* method in a second argument.
      
      * If no help_* method is present for a command, but an opts_* object 
      is, Cmd2 prints the OptionParser's usage string and help text.
      
    """

    def findopt(self,cmd):
	try:
	    o=getattr(self,'opts_'+cmd)
	    if isinstance(o,OptionParser): return o
	except AttributeError:
	    return None
	return None

    def onecmd(self, line):
        """Interpret the argument as though it had been typed in response
        to the prompt.

        This may be overridden, but should not normally need to be;
        see the precmd() and postcmd() methods for useful execution hooks.
        The return value is a flag indicating whether interpretation of
        commands by the interpreter should stop.

        """
        cmd, arg, line = self.parseline(line)
        if not line:
            return self.emptyline()
        if cmd is None:
            return self.default(line)
        self.lastcmd = line
        if cmd == '':
            return self.default(line)
        else:
            try:
                func = getattr(self, 'do_' + cmd)
            except AttributeError:
                return self.default(line)
	    arg=shlex.split(arg)
	    o=self.findopt(cmd)
	    if not o is None:
		opts,args=o.parse_args(arg)
		return func(args,opts)
	    else:
		return func(arg)
