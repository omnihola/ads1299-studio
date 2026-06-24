
#ifndef GUARD_styxmsg_h
#define GUARD_styxmsg_h

//:wrap=soft:

#include <string>
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fcall_t fcall_t;
typedef struct dir_t dir_t;
typedef struct Qid Qid;

enum {
	Tversion =	100,
	Rversion,
	Tauth =		102,
	Rauth,
	Tattach =	104,
	Rattach,
	Terror =	106,	/* illegal */
	Rerror,
	Tflush =	108,
	Rflush,
	Twalk =		110,
	Rwalk,
	Topen =		112,
	Ropen,
	Tcreate =	114,
	Rcreate,
	Tread =		116,
	Rread,
	Twrite =	118,
	Rwrite,
	Tclunk =	120,
	Rclunk,
	Tremove =	122,
	Rremove,
	Tstat =		124,
	Rstat,
	Twstat =	126,
	Rwstat,
	Tmax
};

typedef struct Qid
{
	u32	path;
	u32	vers;
	u8	type;
} Qid;

struct dir_t
{
	/* system-modified data */
	u16	type;	/* server type */
	u16	dev;	/* server subtype */
	/* file data */
	Qid	qid;	/* unique id from server */
// we don't do mod times or users
	u32	mode;	/* permissions */
//	u32	atime;	/* last read time */
//	u32	mtime;	/* last write time */
	long	length;	/* file length */
	const char *name;	/* last element of path */
//	char	*uid;	/* owner name */
//	char	*gid;	/* group name */
//	char	*muid;	/* last modifier name */
};

#define	MAXWELEM	16

struct fcall_t
{
	u8	type;
	u32	fid;
	u16	tag;
	std::string data;
	union {
		struct {
			u32	msize;		/* Tversion, Rversion */
			char	*version;	/* Tversion, Rversion */
		} version;
		u16	oldtag;		/* Tflush */
		char	*ename;		/* Rerror */
		struct {
			Qid	qid;		/* Rattach, Ropen, Rcreate */
			u32	iounit;		/* Ropen, Rcreate */
		} open;
		Qid	aqid;		/* Rauth */
		struct {
			u32	afid;		/* Tauth, Tattach */
			char	*uname;		/* Tauth, Tattach */
			char	*aname;		/* Tauth, Tattach */
		} attach;
		struct {
			u32	perm;		/* Tcreate */ 
			char	*name;		/* Tcreate */
			u8	mode;		/* Tcreate, Topen */
		} create;
		struct {
			u32	newfid;		/* Twalk */
			u16	nwname;		/* Twalk */
			char*	wname[MAXWELEM];	/* Twalk */
		} twalk;
		struct {
			u16	nwqid;		/* Rwalk */
			Qid	wqid[MAXWELEM];		/* Rwalk */
		} rwalk;
		struct {
			u32	offset;		/* Tread, Twrite */
			u32	count;		/* Tread, Twrite, Rread */
			char*	data;		/* Twrite, Rread */
		} rw;
		struct {
			u16	nstat;		/* Twstat, Rstat */
			char*	stat;		/* Twstat, Rstat */
		} stat;
	} u;
};

//! Calculate size of a serialised Fcall
/*! \internal Returns the number of bytes that the given Fcall would occupy as a Styx message.

\param f Fcall to inspect

\return Number of bytes in serialised Fcall
*/
u16 cstyx_sizeS2M(fcall_t *f);

//! Convert an Fcall into a serialised message
/*! \internal Converts Fcall \p f into a Styx message.  The message is placed in a dynamically allocated buffer, which is returned in \p ap.

\param f[inout] Fcall structure to convert
\param msg[out] String to receive result
*/
u16 cstyx_convS2M(fcall_t *f, std::string& msg);

//! Calculate size of a serialised Dir
u16 cstyx_sizeD2M(dir_t *d);

//! Serialise Dir structure
/*!
\internal

\param[in] d Dir structure to serialise
\param[in] buf Buffer to place data in
\param[in] nbuf Length of buffer
\param[in] len Current length of file
*/
u16 cstyx_convD2M(dir_t *d, char *buf, u16 nbuf, u32 len);

//! Convert Styx message to fcall structure
/*! \internal Converts a Styx message to an fcall structure.

\note This function performs no syntactic checks.
three causes for error:
 1. message size field is incorrect
 2. input buffer too short for its own data (counts too long, etc.)
 3. too many names or qids
gqid() and gstring() return nil if they would reach beyond buffer.
main switch statement checks range and also can fall through
to test at end of routine.

\param ap Pointer to packet
\param nap Length of packet
\param f fcall structure to fill in

\retval ~0 \p f is filled in; packet was correct
\retval 0 packet was not correct, \p f is not filled in
 */
u16 cstyx_convM2S(char *ap, u16 nap, fcall_t *f);

u16 cstyx_convM2D(char *buf, u16 nbuf, dir_t *d, char *strs);

int cstyx_statcheck(char *buf, u16 nbuf);

#ifdef __cplusplus
}
#endif

#endif
