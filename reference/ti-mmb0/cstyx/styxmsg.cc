
#include "styxmsg.hh"
#include <stdlib.h>

#define nil		((void*)0)

#define	GBIT8(p)	((u8)((p)[0]&0xff))
#define	GBIT16(p)	( ((u16)((p)[0]&0xff)) | ((u16)((p)[1]&0xff)<<8) )
#define	GBIT32(p)	( ((u32)((p)[0]&0xff)) |\
	((u32)((p)[1]&0xff)<<8) |\
	((u32)((p)[2]&0xff)<<16) |\
	((u32)((p)[3]&0xff)<<24) )

#if 0
s64 GBIT64(u8 *p)
{
	s64 d;

	d.l=GBIT32(p);
	d.h=GBIT32(p);
}
#define	GBIT64(p)	((long long)((p)[0]|((p)[1]<<8)|((p)[2]<<16)|((p)[3]<<24)) |\
				((long long)((p)[4]|((p)[5]<<8)|((p)[6]<<16)|((p)[7]<<24)) << 32))
#endif

#define	PBIT8(p,v)	(p)[0]=((u8)v)
#define	PBIT16(p,v)	(p)[0]=((u16)v)&0xff;(p)[1]=(((u16)v)>>8)&0xff
#define	PBIT32(p,v)	(p)[0]=((u32)v)&0xff;\
	(p)[1]=(((u32)v)>>8)&0xff;\
	(p)[2]=(((u32)v)>>16)&0xff;\
	(p)[3]=(((u32)v)>>24)&0xff

#if 0
void PBIT64(char *p, s64 v)
{
	PBIT32(p,v.l);
	PBIT32(p,v.h);
}

#define	PBIT64(p,v)	(p)[0]=(v);(p)[1]=(v)>>8;(p)[2]=(v)>>16;(p)[3]=(v)>>24;\
			(p)[4]=(v)>>32;(p)[5]=(v)>>40;(p)[6]=(v)>>48;(p)[7]=(v)>>56
#endif

#define	BIT8SZ		1
#define	BIT16SZ		2
#define	BIT32SZ		4
#define	BIT64SZ		8
#define	QIDSZ	(BIT8SZ+BIT32SZ+BIT64SZ)

/* STATFIXLEN includes leading 16-bit count */
/* The count, however, excludes itself; total size is BIT16SZ+count */
#define STATFIXLEN	(BIT16SZ+QIDSZ+5*BIT16SZ+4*BIT32SZ+1*BIT64SZ)	/* amount of fixed length data in a stat buffer */

#define	NOTAG		(u16)~0U	/* Dummy tag */
#define	NOFID		(u32)~0U	/* Dummy fid */
#define	IOHDRSZ		24	/* ample room for Twrite/Rread header (iounit) */

int cstyx_statcheck(char* buf, u16 nbuf)
{
	char* ebuf;
	int i;

	ebuf = buf + nbuf;

	if(nbuf < STATFIXLEN || nbuf != BIT16SZ + GBIT16(buf))
		return -1;

	buf += STATFIXLEN - 4 * BIT16SZ;

	for(i = 0; i < 4; i++){
		if(buf + BIT16SZ > ebuf)
			return -1;
		buf += BIT16SZ + GBIT16(buf);
	}

	if(buf != ebuf)
		return -1;

	return 0;
}

static char nullstring[] = "";

u16 cstyx_convM2D(char* buf, u16 nbuf, dir_t *d, char *strs)
{
	char *p, *ebuf;
	char *sv[4];
	int i, ns;

	if(nbuf < STATFIXLEN)
		return 0; 

	p = buf;
	ebuf = buf + nbuf;

	p += BIT16SZ;	/* ignore size */
	d->type = GBIT16(p);
	p += BIT16SZ;
	d->dev = GBIT32(p);
	p += BIT32SZ;
	d->qid.type = GBIT8(p);
	p += BIT8SZ;
	d->qid.vers = GBIT32(p);
	p += BIT32SZ;
	d->qid.path = GBIT32(p);
	p += BIT64SZ;
	d->mode = GBIT32(p);
	p += BIT32SZ;
	//d->atime = GBIT32(p);
	p += BIT32SZ;
	//d->mtime = GBIT32(p);
	p += BIT32SZ;
	d->length = GBIT32(p);
	p += BIT64SZ;

	for(i = 0; i < 4; i++){
		if(p + BIT16SZ > ebuf)
			return 0;
		ns = GBIT16(p);
		p += BIT16SZ;
		if(p + ns > ebuf)
			return 0;
		if(strs){
			sv[i] = strs;
			memmove(strs, p, ns);
			strs += ns;
			*strs++ = '\0';
		}
		p += ns;
	}

	if(strs){
		d->name = sv[0];
		//d->uid = sv[1];
		//d->gid = sv[2];
		//d->muid = sv[3];
	}else{
		d->name = nullstring;
		//d->uid = nullstring;
		//d->gid = nullstring;
		//d->muid = nullstring;
	}
	
	return p - buf;
}

u16 cstyx_sizeD2M(dir_t *d)
{
	return STATFIXLEN + strlen(d->name)+18;
}

u16 cstyx_convD2M(dir_t *d, char* buf, u16 nbuf, u32 len)
{

	char *p, *ebuf;
	const char *sv[4];
	int i, ns, nsv[4], ss;

	if(nbuf < BIT16SZ)
		return 0;

	p = buf;
	ebuf = buf + nbuf;

	sv[0] = d->name;
	sv[1] = "nobody";
	sv[2] = "nobody";
	sv[3] = "nobody";

	ns = 0;
	for(i = 0; i < 4; i++){
		if(sv[i])
			nsv[i] = strlen(sv[i]);
		else
			nsv[i] = 0;
		ns += nsv[i];
	}

	ss = STATFIXLEN + ns;

	/* set size befor erroring, so user can know how much is needed */
	/* note that length excludes count field itself */
	PBIT16(p, ss-BIT16SZ);
	p += BIT16SZ;

	if(ss > nbuf)
		return BIT16SZ;

	PBIT16(p, d->type);
	p += BIT16SZ;
	PBIT32(p, d->dev);
	p += BIT32SZ;
	PBIT8(p, d->qid.type);
	p += BIT8SZ;
	PBIT32(p, d->qid.vers);
	p += BIT32SZ;
	PBIT32(p, d->qid.path);
	p += BIT32SZ;
	PBIT32(p,0);
	p += BIT32SZ;
	PBIT32(p, d->mode);
	p += BIT32SZ;
	//PBIT32(p, d->atime);
	PBIT32(p,0);
	p += BIT32SZ;
	//PBIT32(p, d->mtime);
	PBIT32(p,0);
	p += BIT32SZ;
	//PBIT64(p, d->length);
	PBIT32(p,len);
	p += BIT32SZ;
	PBIT32(p,0);
	p += BIT32SZ;

	for(i = 0; i < 4; i++){
		ns = nsv[i];
		if(p + ns + BIT16SZ > ebuf)
			return 0;
		PBIT16(p, ns);
		p += BIT16SZ;
		if(ns)
			memmove(p, sv[i], ns);
		p += ns;
	}

	if(ss != p - buf)
		return 0;

	return p - buf;
}

static char* gstring(char* p, char* ep, char **s)
{
	u16 n;

	if(p+BIT16SZ > ep)
		return 0;
	n = GBIT16(p);
	p += BIT16SZ - 1;
	if(p+n+1 > ep)
		return 0;
	/* move it down, on top of count, to make room for '\0' */
	memmove(p, p + 1, n);
	p[n] = '\0';
	*s = (char*)p;
	p += n+1;
	return p;
}

static char* gqid(char* p, char* ep, Qid* q)
{
	if(p+QIDSZ > ep)
		return 0;
	q->type = GBIT8(p);
	p += BIT8SZ;
	q->vers = GBIT32(p);
	p += BIT32SZ;
	q->path = GBIT32(p);
	p += BIT64SZ;
	return p;
}

u16 cstyx_convM2S(char* ap, u16 nap, fcall_t *f)
{
	char *p, *ep;
	u16 i, size;

	p = ap;
	ep = p + nap;

	if(p+BIT32SZ+BIT8SZ+BIT16SZ > ep)
		return 0;
	size = GBIT32(p);
	p += BIT32SZ;

	if (size<BIT32SZ+BIT8SZ+BIT16SZ)
		return 0;

	f->type = GBIT8(p);
	p += BIT8SZ;
	f->tag = GBIT16(p);
	p += BIT16SZ;

	switch(f->type) {
	default:
		return 0;

	case Tversion:
		if(p+BIT32SZ > ep)
			return 0;
		f->u.version.msize = GBIT32(p);
		p += BIT32SZ;
		p = gstring(p, ep, &f->u.version.version);
		break;

	case Tflush:
		if(p+BIT16SZ > ep)
			return 0;
		f->u.oldtag = GBIT16(p);
		p += BIT16SZ;
		break;

	case Tauth:
		if(p+BIT32SZ > ep)
			return 0;
		f->u.attach.afid = GBIT32(p);
		p += BIT32SZ;
		p = gstring(p, ep, &f->u.attach.uname);
		if(p == nil)
			break;
		p = gstring(p, ep, &f->u.attach.aname);
		if(p == nil)
			break;
		break;

	case Tattach:
		if(p+BIT32SZ > ep)
			return 0;
		f->fid = GBIT32(p);
		p += BIT32SZ;
		if(p+BIT32SZ > ep)
			return 0;
		f->u.attach.afid = GBIT32(p);
		p += BIT32SZ;
		p = gstring(p, ep, &f->u.attach.uname);
		if(p == nil)
			break;
		p = gstring(p, ep, &f->u.attach.aname);
		if(p == nil)
			break;
		break;

	case Twalk:
		if(p+BIT32SZ+BIT32SZ+BIT16SZ > ep)
			return 0;
		f->fid = GBIT32(p);
		p += BIT32SZ;
		f->u.twalk.newfid = GBIT32(p);
		p += BIT32SZ;
		f->u.twalk.nwname = GBIT16(p);
		p += BIT16SZ;
		if(f->u.twalk.nwname > MAXWELEM)
			return 0;
		for(i=0; i<f->u.twalk.nwname; i++){
			p = gstring(p, ep, &f->u.twalk.wname[i]);
			if(p == nil)
				break;
		}
		break;

	case Topen:
		if(p+BIT32SZ+BIT8SZ > ep)
			return 0;
		f->fid = GBIT32(p);
		p += BIT32SZ;
		f->u.create.mode = GBIT8(p);
		p += BIT8SZ;
		break;

	case Tcreate:
		if(p+BIT32SZ > ep)
			return 0;
		f->fid = GBIT32(p);
		p += BIT32SZ;
		p = gstring(p, ep, &f->u.create.name);
		if(p == nil)
			break;
		if(p+BIT32SZ+BIT8SZ > ep)
			return 0;
		f->u.create.perm = GBIT32(p);
		p += BIT32SZ;
		f->u.create.mode = GBIT8(p);
		p += BIT8SZ;
		break;

	case Tread:
		if(p+BIT32SZ+BIT64SZ+BIT32SZ > ep)
			return 0;
		f->fid = GBIT32(p);
		p += BIT32SZ;
		f->u.rw.offset = GBIT32(p);
		p += BIT64SZ;
		f->u.rw.count = GBIT32(p);
		p += BIT32SZ;
		break;

	case Twrite:
		if(p+BIT32SZ+BIT64SZ+BIT32SZ > ep)
			return 0;
		f->fid = GBIT32(p);
		p += BIT32SZ;
		f->u.rw.offset = GBIT32(p);
		p += BIT64SZ;
		f->u.rw.count = GBIT32(p);
		p += BIT32SZ;
		if(p+f->u.rw.count > ep)
			return 0;
		f->u.rw.data = p;
		p += f->u.rw.count;
		break;

	case Tclunk:
	case Tremove:
		if(p+BIT32SZ > ep)
			return 0;
		f->fid = GBIT32(p);
		p += BIT32SZ;
		break;

	case Tstat:
		if(p+BIT32SZ > ep)
			return 0;
		f->fid = GBIT32(p);
		p += BIT32SZ;
		break;

	case Twstat:
		if(p+BIT32SZ+BIT16SZ > ep)
			return 0;
		f->fid = GBIT32(p);
		p += BIT32SZ;
		f->u.stat.nstat = GBIT16(p);
		p += BIT16SZ;
		if(p+f->u.stat.nstat > ep)
			return 0;
		f->u.stat.stat = p;
		p += f->u.stat.nstat;
		break;
/*
 */
	case Rversion:
		if(p+BIT32SZ > ep)
			return 0;
		f->u.version.msize = GBIT32(p);
		p += BIT32SZ;
		p = gstring(p, ep, &f->u.version.version);
		break;

	case Rerror:
		p = gstring(p, ep, &f->u.ename);
		break;

	case Rflush:
		break;

	case Rauth:
		p = gqid(p, ep, &f->u.aqid);
		if(p == nil)
			break;
		break;

	case Rattach:
		p = gqid(p, ep, &f->u.open.qid);
		if(p == nil)
			break;
		break;

	case Rwalk:
		if(p+BIT16SZ > ep)
			return 0;
		f->u.rwalk.nwqid = GBIT16(p);
		p += BIT16SZ;
		if(f->u.rwalk.nwqid > MAXWELEM)
			return 0;
		for(i=0; i<f->u.rwalk.nwqid; i++){
			p = gqid(p, ep, &f->u.rwalk.wqid[i]);
			if(p == nil)
				break;
		}
		break;

	case Ropen:
	case Rcreate:
		p = gqid(p, ep, &f->u.open.qid);
		if(p == nil)
			break;
		if(p+BIT32SZ > ep)
			return 0;
		f->u.open.iounit = GBIT32(p);
		p += BIT32SZ;
		break;

	case Rread:
		if(p+BIT32SZ > ep) return 0;
		f->u.rw.count = GBIT32(p);
		p += BIT32SZ;
		if(p+f->u.rw.count > ep)
			return 0;
		f->u.rw.data = p;
		p += f->u.rw.count;
		break;

	case Rwrite:
		if(p+BIT32SZ > ep)
			return 0;
		f->u.rw.count = GBIT32(p);
		p += BIT32SZ;
		break;

	case Rclunk:
	case Rremove:
		break;

	case Rstat:
		if(p+BIT16SZ > ep)
			return 0;
		f->u.stat.nstat = GBIT16(p);
		p += BIT16SZ;
		if(p+f->u.stat.nstat > ep)
			return 0;
		f->u.stat.stat = p;
		p += f->u.stat.nstat;
		break;

	case Rwstat:
		break;
	}

	if(p==nil || p>ep)
		return 0;
	if(ap+size == p)
		return size;
	return 0;
}

//! Write a Pascal string
/*! \internal Writes \p s as a Pascal string to \p p.  The string begins with a 16-bit length.

\param p Output buffer
\param s Input C string

\return Resulting Pascal string
*/
static char* pstring(char* p, char *s)
{
	u16 n;

	if(s == nil){
		PBIT16(p, 0);
		p += BIT16SZ;
		return p;
	}

	n = strlen(s);
	PBIT16(p, n);
	p += BIT16SZ;
	memmove(p, s, n);
	p += n;
	return p;
}

//! Serialise a Qid
/*! \internal Serialises Qid \p q to buffer \p p.

\param p Output buffer
\param q Qid to serialise

\return Result
*/
static char* pqid(char* p, Qid *q)
{
	PBIT8(p, q->type);
	p += BIT8SZ;
	PBIT32(p, q->vers);
	p += BIT32SZ;
	PBIT32(p, q->path);
	p += BIT32SZ;
	PBIT32(p,0);
	p+=BIT32SZ;
	return p;
}

//! Return string size plus length
/*! \internal Returns number of characters in \p s plus two bytes for length.  This is the length the string would occupy if converted to a Pascal string using pstring().

\param s String to examine
\return Size of "Pascalised" string
*/
static u16 stringsz(char* s)
{
	if(s == nil)
		return BIT16SZ;

	return BIT16SZ+strlen(s);
}

u16 cstyx_sizeS2M(fcall_t *f)
{
	u16 n;
        int i;

	n = 0;
	n += BIT32SZ;	/* size */
	n += BIT8SZ;	/* type */
	n += BIT16SZ;	/* tag */

	switch(f->type)
	{
	default:
		return 0;
	case Tversion:
		n += BIT32SZ;
		n += stringsz(f->u.version.version);
		break;

	case Tflush:
		n += BIT16SZ;
		break;

	case Tauth:
		n += BIT32SZ;
		n += stringsz(f->u.attach.uname);
		n += stringsz(f->u.attach.aname);
		break;

	case Tattach:
		n += BIT32SZ;
		n += BIT32SZ;
		n += stringsz(f->u.attach.uname);
		n += stringsz(f->u.attach.aname);
		break;

	case Twalk:
		n += BIT32SZ;
		n += BIT32SZ;
		n += BIT16SZ;
		for(i=0; i<f->u.twalk.nwname; i++)
			n += stringsz(f->u.twalk.wname[i]);
		break;

	case Topen:
		n += BIT32SZ;
		n += BIT8SZ;
		break;

	case Tcreate:
		n += BIT32SZ;
		n += stringsz(f->u.create.name);
		n += BIT32SZ;
		n += BIT8SZ;
		break;

	case Tread:
		n += BIT32SZ;
		n += BIT64SZ;
		n += BIT32SZ;
		break;

	case Twrite:
		n += BIT32SZ;
		n += BIT64SZ;
		n += BIT32SZ;
		n += f->u.rw.count;
		break;

	case Tclunk:
	case Tremove:
		n += BIT32SZ;
		break;

	case Tstat:
		n += BIT32SZ;
		break;

	case Twstat:
		n += BIT32SZ;
		n += BIT16SZ;
		n += f->u.stat.nstat;
		break;
/*
 */
	case Rversion:
		n += BIT32SZ;
		n += stringsz(f->u.version.version);
		break;

	case Rerror:
		n += stringsz(f->u.ename);
		break;

	case Rflush:
		break;

	case Rauth:
		n += QIDSZ;
		break;

	case Rattach:
		n += QIDSZ;
		break;

	case Rwalk:
		n += BIT16SZ;
		n += f->u.rwalk.nwqid*QIDSZ;
		break;

	case Ropen:
	case Rcreate:
		n += QIDSZ;
		n += BIT32SZ;
		break;

	case Rread:
		n += BIT32SZ;
		n += f->u.rw.count;
		break;

	case Rwrite:
		n += BIT32SZ;
		break;

	case Rclunk:
		break;

	case Rremove:
		break;

	case Rstat:
		n += BIT16SZ;
		n += f->u.stat.nstat;
		break;

	case Rwstat:
		break;
	}
	return n;
}

u16 cstyx_convS2M(fcall_t* f, std::string& msg)
{
	char* p;
        char* ap;
	u16 i, size;

	size = cstyx_sizeS2M(f);
	if(size == 0) return 0;
	ap=(char*)malloc(size);
	if (!ap) return 0;
	p = ap;

	if (f->type==Rread) {
		PBIT32(p, size+f->u.rw.count);
	} else {
		PBIT32(p, size);
	}
	p += BIT32SZ;
	PBIT8(p, f->type);
	p += BIT8SZ;
	PBIT16(p, f->tag);
	p += BIT16SZ;

	switch(f->type)
	{
	default:
		return 0;
	case Tversion:
		PBIT32(p, f->u.version.msize);
		p += BIT32SZ;
		p = pstring(p, f->u.version.version);
		break;

	case Tflush:
		PBIT16(p, f->u.oldtag);
		p += BIT16SZ;
		break;

	case Tauth:
		PBIT32(p, f->u.attach.afid);
		p += BIT32SZ;
		p  = pstring(p, f->u.attach.uname);
		p  = pstring(p, f->u.attach.aname);
		break;

	case Tattach:
		PBIT32(p, f->fid);
		p += BIT32SZ;
		PBIT32(p, f->u.attach.afid);
		p += BIT32SZ;
		p  = pstring(p, f->u.attach.uname);
		p  = pstring(p, f->u.attach.aname);
		break;

	case Twalk:
		PBIT32(p, f->fid);
		p += BIT32SZ;
		PBIT32(p, f->u.twalk.newfid);
		p += BIT32SZ;
		PBIT16(p, f->u.twalk.nwname);
		p += BIT16SZ;
		if(f->u.twalk.nwname > MAXWELEM)
			return 0;
		for(i=0; i<f->u.twalk.nwname; i++)
			p = pstring(p, f->u.twalk.wname[i]);
		break;

	case Topen:
		PBIT32(p, f->fid);
		p += BIT32SZ;
		PBIT8(p, f->u.create.mode);
		p += BIT8SZ;
		break;

	case Tcreate:
		PBIT32(p, f->fid);
		p += BIT32SZ;
		p = pstring(p, f->u.create.name);
		PBIT32(p, f->u.create.perm);
		p += BIT32SZ;
		PBIT8(p, f->u.create.mode);
		p += BIT8SZ;
		break;

	case Tread:
		PBIT32(p, f->fid);
		p += BIT32SZ;
		PBIT32(p, f->u.rw.offset);
		p += BIT32SZ;
		PBIT32(p,0);
		p+=BIT32SZ;
		PBIT32(p, f->u.rw.count);
		p += BIT32SZ;
		break;

	case Twrite:
		PBIT32(p, f->fid);
		p += BIT32SZ;
		PBIT32(p, f->u.rw.offset);
		p += BIT32SZ;
		PBIT32(p,0);
		p+=BIT32SZ;
		PBIT32(p, f->u.rw.count);
		p += BIT32SZ;
		memmove(p, f->u.rw.data, f->u.rw.count);
		p += f->u.rw.count;
		break;

	case Tclunk:
	case Tremove:
		PBIT32(p, f->fid);
		p += BIT32SZ;
		break;

	case Tstat:
		PBIT32(p, f->fid);
		p += BIT32SZ;
		break;

	case Twstat:
		PBIT32(p, f->fid);
		p += BIT32SZ;
		PBIT16(p, f->u.stat.nstat);
		p += BIT16SZ;
		memmove(p, f->u.stat.stat, f->u.stat.nstat);
		p += f->u.stat.nstat;
		break;
/*
 */

	case Rversion:
		PBIT32(p, f->u.version.msize);
		p += BIT32SZ;
		p = pstring(p, f->u.version.version);
		break;

	case Rerror:
		p = pstring(p, f->u.ename);
		break;

	case Rflush:
		break;

	case Rauth:
		p = pqid(p, &f->u.aqid);
		break;

	case Rattach:
		p = pqid(p, &f->u.open.qid);
		break;

	case Rwalk:
		PBIT16(p, f->u.rwalk.nwqid);
		p += BIT16SZ;
		if(f->u.rwalk.nwqid > MAXWELEM)
			return 0;
		for(i=0; i<f->u.rwalk.nwqid; i++)
			p = pqid(p, &f->u.rwalk.wqid[i]);
		break;

	case Ropen:
	case Rcreate:
		p = pqid(p, &f->u.open.qid);
		PBIT32(p, f->u.open.iounit);
		p += BIT32SZ;
		break;

	case Rread:
		PBIT32(p, f->u.rw.count);
		p += BIT32SZ;
		//-- we pass data separately -- see wr()
		//memmove(p, f->u.rw.data, f->u.rw.count);
		//p += f->u.rw.count;
		break;

	case Rwrite:
		PBIT32(p, f->u.rw.count);
		p += BIT32SZ;
		break;

	case Rclunk:
		break;

	case Rremove:
		break;

	case Rstat:
		PBIT16(p, f->u.stat.nstat);
		p += BIT16SZ;
		memmove(p, f->u.stat.stat, f->u.stat.nstat);
		p += f->u.stat.nstat;
		break;

	case Rwstat:
		break;
	}
        if(size != p-ap) {
            free(ap);
	    return 0;
        }
        msg.assign(ap,size);
        free(ap);
	return size;
}
