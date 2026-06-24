#!python

FILTER=''.join([(len(repr(chr(x)))==3) and chr(x) or '.' for x in range(256)])

def dump(src, length=8):
    result=[]
    for i in xrange(0, len(src), length):
	s = src[i:i+length]
	hexa = ' '.join(["%02X"%ord(x) for x in s])
	printable = s.translate(FILTER)
	result.append("%04X   %-*s   %s\n" % (i, length*3, hexa, printable))
    return ''.join(result)

if __name__=='__main__':
    s=("This 10 line function is just a sample of pyhton power "
       "for string manipulations.\n"
       "The code is \x07even\x08 quite readable!")
    
    print dump(s)
    print
    print dump2(s)
