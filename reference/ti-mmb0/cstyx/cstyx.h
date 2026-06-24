
#ifndef GUARD_cstyx_c_h
#define GUARD_cstyx_c_h

//:wrap=soft:

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef BUILD_DLL
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif

#define CSTYX_READ 1
#define CSTYX_WRITE 2
#define CSTYX_RDWR 3

typedef struct cstyx_t cstyx_t;
typedef struct cstyx_connection_t cstyx_connection_t;
typedef struct cstyx_client_t cstyx_client_t;
typedef struct cstyx_file_t cstyx_file_t;
typedef struct cstyx_dir_t cstyx_dir_t;
typedef struct cstyx_dirent_t cstyx_dirent_t;

//! Open a client
/*! Open a client using using connection \p c.  Returns a new client object, or null on error.

In addition to establishing a connection, this performs a Tattach and sets the CWD to root.

\param[in] l Connection
\return Client object, or null on error
*/
EXPORT cstyx_client_t* cstyx_connect(cstyx_connection_t* c);
EXPORT void cstyx_close_connection(cstyx_connection_t* c);

EXPORT void cstyx_close_client(cstyx_client_t* c);

//! Read a directory
EXPORT cstyx_dir_t* cstyx_read_dir(cstyx_client_t* c, const char* p);
//! Find out whether a file is a directory
EXPORT int cstyx_path_isdir(const char*);
//! Find out whether a file is a directory from its directory entry
EXPORT int cstyx_dirent_isdir(cstyx_dirent_t* e);
//! Get the number of entries in a directory
EXPORT int cstyx_get_dir_length(cstyx_dir_t* d);
//! Get the (normalised) path of a directory
EXPORT const char* cstyx_get_dir_path(cstyx_dir_t* d);
//! Get a directory entry from a directory
EXPORT cstyx_dirent_t* cstyx_get_dirent(cstyx_dir_t* d, int i);
//! Finish reading a directory
EXPORT void cstyx_close_dir(cstyx_dir_t* d);
//! Get mode from a directory entry
EXPORT long cstyx_get_dirent_mode(cstyx_dirent_t* e);
//! Get name from a directory entry
EXPORT const char* cstyx_get_dirent_name(cstyx_dirent_t* e);
//! Get file length from a directory entry
EXPORT u32 cstyx_get_dirent_len(cstyx_dirent_t* e);
//! Dispose of a directory entry
EXPORT void cstyx_delete_dirent(cstyx_dirent_t* d);

//! Get current working directory
EXPORT const char* cstyx_get_cwd(cstyx_client_t* d);
//! Set current working directory
EXPORT int cstyx_set_cwd(cstyx_client_t* d, const char* path);
//! Get file information
EXPORT cstyx_dirent_t* cstyx_stat(cstyx_file_t* f);
//! Get the length of a file
EXPORT long cstyx_get_file_len(cstyx_file_t* f);
//! Open a file
EXPORT cstyx_file_t* cstyx_open(cstyx_client_t* c, const char* path, int mode);
//! Close a file
EXPORT void cstyx_close(cstyx_file_t* f);
//! Read from a file
EXPORT int cstyx_read(cstyx_file_t* f, char* d, u16* len);
//! Read from a path until EOF
EXPORT int cstyx_read_all_path(cstyx_client_t* c, const char* path, char* s, int lim);
//! Write to a file
EXPORT int cstyx_write(cstyx_file_t* f, const char* d, u16* len);
//! Write to a path
EXPORT int cstyx_write_path(cstyx_client_t* f, const char* path, const char* d, int len);

//! Retrieve the latest error message
EXPORT const char* cstyx_get_latest_error(int* d);

#ifdef __cplusplus
}
#endif

#endif
