#ifndef HEADER_fd_src_pod_fd_pod_h
#define HEADER_fd_src_pod_fd_pod_h

/* pod is a set a of APIs for managing flexible hierarchies of typed
   key-val pairs.  A pod is a data structure for holds these in memory
   contiguously and compactly such that it can be easily saved to
   permanent storage, sent over networks, distributed between different
   hosts / architectures / address spaces.

   It is trivial to make a pod.

   It is trivial to query a pod.

   It is trivial to import different config file formats into a pod
   (including JSON, YAML, TOML, etc).

   It is trivial to serialize / deserialize / save / restore a pod.

   Multiple value types are supported with builtin coverage of all
   primitive datatypes.  In particular, a value itself can be a pod and
   it is easy to lookup deeply nested values in a pod via their key
   path.  (Essentially, a pod a simple in-memory file system.)

   As such pods are an incredible useful building blocks for dealing
   with heterogeneous distributed environment / configuration,
   checkpointing, etc.

   The current implementation of POD below assumes little endian
   architecture and that the platform can reasonably efficient access
   unaligned primitive types.  These restrictions can be removed if
   necessary.

   A pod starts with 3 svw (symmetric-variable-width) encoded ulongs:

   - max:  The max size of the pod in bytes (including the header)
   - used: The number of bytes currently used in the pod in bytes
           (including the header), <= max.
   - cnt:  The number of key-val pairs in the pod (a key-subpod pair is
           considered as a single pair from the header POV regardless of
           the number of keys the subpod might hold).  As a key val pair
           requires at least 1 byte to represent practically, this is
           <=used (and more typically << used).

   Since cnt<=used<=max, the svw encoded size of all these are bounded
   by the encoded max and we can thus use the same size encoding to
   facilitate fast operations on encoded headers.

   This header is followed by cnt key-val pairs.  A pair is represented
   in a pod as:

   - key_sz:   strlen(key)+1 to facilitate fast iteration and fast key
               string ops
   - key:      key_sz_bytes holds the key cstr, key does not contain a
               '.' (to facilitate recursive querying), does include the
               '\0' to facilitate zero-copy user operation /
               interoperability with standard cstr handling APIs
   - val_type: 1 byte, a FD_POD_VAL_TYPE_* for extensibility
   - val_sz:   number of bytes in in the pod encoded representation
               of val
   - val:      val_sz bytes, interpreted as specified by val_type

   key_sz and val_sz are both svw encoded.  There are no theoretical
   restrictions (up to the size of a ulong) on the size of a key or a
   val. */

#include "../bits/fd_bits.h"

/* FD_POD_ERR_* gives a number of error codes used by fd_pod APIs. */

#define FD_POD_SUCCESS     ( 0) /* Operation was successful */
#define FD_POD_ERR_INVAL   (-1) /* Operation failed because input args were invalid */
#define FD_POD_ERR_TYPE    (-2) /* Operation failed because the path contained a key of an unexpected type */
#define FD_POD_ERR_RESOLVE (-3) /* Operation failed because the path did not resolve to a key */
#define FD_POD_ERR_FULL    (-4) /* Operation failed because the pod did not have enough space to complete it */

/* FD_POD_VAL_TYPE_* gives a type of value stored in a pod key-val pair.
   These must be in [0,255].  Values in [16,127] are reserved for
   potentially additional primitive types.  Vales in [128,255] are
   reserved for user defined types. */

#define FD_POD_VAL_TYPE_SUBPOD  ( 0) /* Val is a RAW encoded pod */
#define FD_POD_VAL_TYPE_BUF     ( 1) /* Val is a RAW encoded buffer */
#define FD_POD_VAL_TYPE_CSTR    ( 2) /* Val is a RAW encoded '\0'-terminated string */
#define FD_POD_VAL_TYPE_CHAR    ( 3) /* Val is a RAW encoded   8-bit char (indeterminant sign) */
#define FD_POD_VAL_TYPE_SCHAR   ( 4) /* Val is a RAW encoded   8-bit signed int (twos complement) */
#define FD_POD_VAL_TYPE_SHORT   ( 5) /* Val is a SVW encoded  16-bit signed int (twos complement) */
#define FD_POD_VAL_TYPE_INT     ( 6) /* Val is a SVW encoded  32-bit signed int (twos complement) */
#define FD_POD_VAL_TYPE_LONG    ( 7) /* Val is a SVW encoded  64-bit signed int (twos complement) */
#define FD_POD_VAL_TYPE_INT128  ( 8) /* Val is a SVW encoded 128-bit signed int (twos complement) */
#define FD_POD_VAL_TYPE_UCHAR   ( 9) /* Val is a RAW encoded   8-bit unsigned int */
#define FD_POD_VAL_TYPE_USHORT  (10) /* Val is a SVW encoded  16-bit unsigned int */
#define FD_POD_VAL_TYPE_UINT    (11) /* Val is a SVW encoded  32-bit unsigned int */
#define FD_POD_VAL_TYPE_ULONG   (12) /* Val is a SVW encoded  64-bit unsigned int */
#define FD_POD_VAL_TYPE_UINT128 (13) /* Val is a SVW encoded 128-bit unsigned int */
#define FD_POD_VAL_TYPE_FLOAT   (14) /* Val is a RAW IEEE-754 float  (little endian) */
#define FD_POD_VAL_TYPE_DOUBLE  (15) /* Val is a RAW IEEE-754 double (little endian) */

/* FD_POD_FOOTPRINT_MIN gives the minimum pod byte footprint possible */

#define FD_POD_FOOTPRINT_MIN (3UL)

/* A fd_pod_info_t is used when listing the contents of a pod.  It is
   not stored explicitly in the pod itself.  The lifetime guarantees of
   all pointers in an info that of the pod itself or any invalidating
   operaiton on that pod. */

struct fd_pod_info;
typedef struct fd_pod_info fd_pod_info_t;

struct fd_pod_info {
  ulong           key_sz;   /* Size of key in pod (includes terminating '\0') */
  char const *    key;      /* Pointer to first byte of this pod key cstr */
  int             val_type; /* Type of val (in [0,255], a FD_POD_VAL_TYPE_*) */
  ulong           val_sz;   /* Size of val in bytes (pod encoded form) */
  void const *    val;      /* Pointer to first byte of val (in pod encoded form).  For a cstr type, if val_sz==0, ignore this and
                               treat as NULL (FIXME: CONSIDER HANDLING THIS UNDER THE HOOD?).  */
  fd_pod_info_t * parent;   /* For a recursive listing, NULL if the key is not in a subpod of the pod getting listed.  Otherwise,
                               points to an (earlier) info with details about the subpod.  For a non-recursive listing or query,
                               NULL. */
};

typedef struct fd_pod_info fd_pod_info_t;

FD_PROTOTYPES_BEGIN

/* Constructors *******************************************************/

/* fd_pod_{align,footprint,new,join,leave,delete} are the distributed
   shared memory constructors for a pod and have the usual semantics.

   Note max is the number of bytes available for the whole pod.
   Further, there is no actual alignment requirement.  This allows
   flexibly storing pods into all sorts of places with arbitrary size
   and alignment constraints.

   The only practical constraint is a pod can not be squeezed into a
   region smaller than FD_POD_FOOTPRINT_MIN.  Note further that, from
   the point of view of distribution, a pod is just a bag of up to max
   bytes.  Only bytes [0,used) are needed to encode the exact state of
   pod.  Setting max==used effectively seals up a pod such that no more
   key-val pairs can be added to it. */

FD_FN_CONST static inline ulong fd_pod_align    ( void      ) { return 1UL; }
FD_FN_CONST static inline ulong fd_pod_footprint( ulong max ) { return max; } /* Assumes max>=FD_POD_FOOTPRINT_MIN */

static inline void *
fd_pod_new( void * shmem,
            ulong  max ) { /* Assumes max>=FD_POD_FOOTPRINT_MIN */
  uchar * pod = (uchar *)shmem;
  ulong csz = fd_ulong_svw_enc_sz( max );
  fd_ulong_svw_enc_fixed( pod,           csz, max     );
  fd_ulong_svw_enc_fixed( pod + csz,     csz, 3UL*csz ); /* used */
  fd_ulong_svw_enc_fixed( pod + csz*2UL, csz, 0UL     );
  return shmem;
}

static inline uchar * fd_pod_join  ( void        * shpod ) { return (uchar *)shpod; }
static inline void  * fd_pod_leave ( uchar const * pod   ) { return (void *)pod;    }
static inline void  * fd_pod_delete( void        * shpod ) { return shpod;          }

/* Accessors **********************************************************/

/* fd_pod_{max,used,cnt,avail} returns the maximum number of bytes /
   number of used bytes / number of keys / number of bytes available for
   storing key-val pairs in the pod.  Assumes pod is a current local
   join. */

FD_FN_PURE static inline ulong
fd_pod_max( uchar const * pod ) {
  ulong csz = fd_ulong_svw_dec_sz( pod );
  return fd_ulong_svw_dec_fixed( pod, csz );
}

FD_FN_PURE static inline ulong
fd_pod_used( uchar const * pod ) {
  ulong csz = fd_ulong_svw_dec_sz( pod );
  return fd_ulong_svw_dec_fixed( pod + csz, csz );
}

FD_FN_UNUSED FD_FN_PURE static ulong /* Work around -Winline */
fd_pod_cnt( uchar const * pod ) {
  ulong csz = fd_ulong_svw_dec_sz( pod );
  return fd_ulong_svw_dec_fixed( pod + 2UL*csz, csz );
}

FD_FN_PURE static inline ulong
fd_pod_avail( uchar const * pod ) {
  ulong csz = fd_ulong_svw_dec_sz( pod );
  return fd_ulong_svw_dec_fixed( pod, csz ) - fd_ulong_svw_dec_fixed( pod + csz, csz );
}

/* fd_pod_list returns the details about the current key-val pairs in
   the pod.  info is indexed [0,fd_pod_cnt(pod)).  Does not recurse into
   any subpods in the pod.  E.g. for the pod:

     int foo 1
     pod bar {
       int baz {
         int bay 2
         int bax 3
       }
       int baw 4
     }
     int bav 5

   list will return 3 key-val pairs:

     0: int foo 1       (no parent)
     1: pod bar { ... } (no parent)
     2: int bav 5       (no parent)

   Returns info.  The indices used for the current pairs will be stable
   for the pod's lifetime or the next invalidating operation.   Returns
   info on success and NULL on failure (i.e. pod is NULL). */

fd_pod_info_t *
fd_pod_list( uchar const   * FD_RESTRICT pod,
             fd_pod_info_t * FD_RESTRICT info );

/* fd_pod_list_recursive is the same as fd_pod_list but will recurse
   into subpods.  info is indexed [0,fd_pod_cnt_recursive(pod)).
   E.g. for the above example, list_recursive will return 7 key-val
   pairs:

     0: int foo 1       (no parent)
     1: pod bar { ... } (no parent)
     2: pod baz { ... } (parent bar)
     3: int bay 2       (parent baz)
     4: int bax 3       (parent baz)
     5: int baw 4       (parent bar)
     6: int bav 5       (no parent) */

FD_FN_PURE ulong
fd_pod_cnt_recursive( uchar const * pod );

fd_pod_info_t *
fd_pod_list_recursive( uchar const   * FD_RESTRICT pod,
                       fd_pod_info_t * FD_RESTRICT info );

/* fd_pod_query queries the pod for information about path.  Path is a
   cstr that consists of one or more keys delimited with a '.' such
   that, for example, the path:

     "foo.bar.baz"

   indicates the query should find the key foo in the pod, recurse into
   the foo's subpod val, find the key bar in the subpod, recurse in
   bar's subpod val and then find the key baz in the subsubpod and then
   extract information about baz as requested.  Returns 0 on success or
   a non-zero (FD_POD_ERR_*) error code on failure:

     SUCCESS - the query was successful.  If opt_info was non-NULL,
               *opt_info will contain details about the found key.
               See fd_pod_info_t for more details.
     INVAL   - bad input args (e.g. NULL pod and/or NULL path was NULL)
               opt_info ignored
     TYPE    - one of the path prefixes resolved to a non-subpod
               (e.g. "foo.bar" doesn't refer to a subpod)
               opt_info ignored (FIXME: CONSIDER DETAILS IN OPT_INFO?)
     RESOLVE - the path did not resolve to a key
               (e.g. pod doesn't contain a key "foo" or subpod "foo"
               doesn't contain a key named bar or "foo.bar"
               doesn't contain a key baz)
               opt_info ignored (FIXME: CONSIDER DETAILS IN OPT_INFO?)

   info parent will be NULL (even if path nested) */

int
fd_pod_query( uchar const   * FD_RESTRICT pod,
              char const    * FD_RESTRICT path,
              fd_pod_info_t * FD_RESTRICT opt_info );

/* Miscellaneous APIs ************************************************/

/* fd_pod_strerror converts an FD_POD_SUCCESS / FD_POD_ERR_* code into
   a human readable cstr.  The lifetime of the returned pointer is
   infinite.  The returned pointer is always to a non-NULL cstr. */

FD_FN_CONST char const *
fd_pod_strerror( int err );

/* fd_pod_reset throws away all key-val pairs in pod.  (This also throws
   away any key-val pairs in any subpods in the pod.)  Returns pod on
   success and NULL on failure.

   IMPORTANT!  THIS IS AN INVALIDATING OPERATION */

static inline uchar *
fd_pod_reset( uchar * pod ) {
  if( FD_UNLIKELY( !pod ) ) return NULL;
  ulong csz = fd_ulong_svw_dec_sz( pod );
  fd_ulong_svw_enc_fixed( pod + csz,     csz, 3UL*csz ); /* used */
  fd_ulong_svw_enc_fixed( pod + csz*2UL, csz, 0UL     ); /* cnt */
  return pod;
}

/* fd_pod_resize resizes a pod to the largest possible value <= new_max.
   Returns the achieved max on success and 0 on failure (pod is NULL,
   new_max<pod used).  Achieved max is usually new_max but there are
   rare edge cases.  E.g. pod_max==64, pod_used==64, new_max==65 ... the
   pod header needs to be expanded by 3 bytes to to accommodate new_max
   (and potentially wider pod_used and pod_cnt) but that leaves 2 few
   bytes space to encode the existing pod key-val pairs.

   The difference between requested new_max and the achieved new_max is
   typically so small in these edge cases as to be programmatically
   irrelevant (e.g. there wouldn't be enough room to add additional
   key-val pairs to the pod for example).  Users can trap if the return
   value != new_max on return to detect such edge cases if desired
   though.

   That is, if the pod points to the first byte of a pod currently held
   in memory region of new_max bytes in size (where pod used<=new_max),
   this will adjust pod max to make much of the new memory region as
   possible available to the pod for adding new key-val pairs.

   This operation is O(pod_used) worst case.

   IMPORTANT!  THIS IS AN INVALIDATING OPERATION */

ulong
fd_pod_resize( uchar * pod,
               ulong   new_max );

/* fd_pod_compact eliminates any internal padding in the pod.  Assumes
   pod is a current local join.  If full is non-zero, a full compaction
   is done such that the pod_max is reduced to be equal to pod_used and
   the pod header is accordingly compacted (otherwise, the pod_max will
   be unchanged on return).
   
   Regardless of full, all subpods will be recursively fully compacted
   and all cstrs in the pod will have had their padding removed (they
   will be still be '\0' terminated if originally correctly '\0'
   terminated).  Returns the compacted size of the pod on success and 0
   on failure (e.g. pod is NULL).

   IMPORTANT!  THIS IS AN INVALIDATING OPERATION

   IMPORTANT!  DOING A COMPACT FOLLOWED BY A RESIZE IS NOT GUARANTEED TO
   RESTORE THESE ORIGINAL OFFSETS. */

ulong
fd_pod_compact( uchar * pod,
                int     full );

/* General alloc APIs *************************************************/

/* fd_pod_alloc allocates space in the pod for a key at the end of the
   given path with the given val_type whose encoded size is val_sz.
   Returns offset in pod where val should be stored (room for val_sz
   bytes), 0 on failure.  Failure reasons include NULL pod, NULL path,
   one of the path prefixes resolved to a non-subpod, path is already in
   the pod, invalid val_type or no room in pod for val_sz.
   
   If subpods along the path do not exist, they will be created in the
   process.

   IMPORTANT!  THIS IS AN INVALIDATING OPERATION

   IMPORTANT!  In the current implementation, it is possible for one or
   more subpods along the path to be created and the call to fail.  The
   last subpod created in a string of such will be empty.

   Usage with val_types in one of the preexisting FD_POD_VAL_TYPE_*
   probably should use the specific APIs already provided for these
   types instead of this.  This is more to support custom user types. */

ulong
fd_pod_alloc( uchar      * FD_RESTRICT pod,
              char const * FD_RESTRICT path,
              int                      val_type,
              ulong                    val_sz );

/* fd_pod_insert is same as the above but also populates the allocated
   space with the val_sz bytes pointed to by val.  Assumes that val_type
   / val_sz / val encoding is sensible. */

FD_FN_UNUSED static ulong /* Work around -Winline */
fd_pod_insert( uchar      * FD_RESTRICT pod,
               char const * FD_RESTRICT path,
               int                      val_type,
               ulong                    val_sz,
               void const * FD_RESTRICT val ) {
  ulong off = fd_pod_alloc( pod, path, val_type, val_sz );
  if( FD_LIKELY( off ) ) fd_memcpy( pod + off, val, val_sz );
  return off;
}

/* fd_pod_remove removes a key from the pod.  The key is at the end
   of the given path.  E.g. if path is:

     "foo.bar.baz"

   The key "baz" will be remove from subsubpod bar (which in turn is in
   subpod foo).  The pod and/or any subpods on the path WILL NOT be
   compacted after remove.

   If a path ends on a subpod, that subpod and all its keys (and
   subpods) it might contain will be removed.

   Currently, if the removal results in any empty subpod, that subpod
   will be preserved.  (FIXME: CONSIDER OPTION TO REMOVE CREATED EMPTY
   SUBPODS RECURSIVELY TOO?)

   Returns a 0 (FD_POD_SUCCESS) on success or a negative value
   (FD_POD_ERR_*) on failure.  Reasons for failure are:

     INVAL   - bad input args
               (e.g. pod or path was NULL)
     TYPE    - one of the path prefixes resolved to a non-subpod
               (e.g. "foo.bar" above was had a cstr value)
     RESOLVE - the path did not resolve to a key
               (e.g. subsubpod bar did not contain a key baz)

   IMPORTANT!  THIS IS AN INVALIDATING OPERATION */

int
fd_pod_remove( uchar      * FD_RESTRICT pod,
               char const * FD_RESTRICT path );

/* Specific alloc APIs ************************************************/

/* fd_pod_alloc_subpod creates a empty subpod at path with space for up
   to max bytes in the given pod.  Returns offset of subpod within the
   pod on success (e.g. pod + off is the location of an unjoined pod)
   and 0 on failure.  The user can add key-val pairs within this subpod
   as it would any pod with created with max storage.  This offset is
   valid for the pod's lifetime or an invalidating operation is done on
   the pod.

   IMPORTANT!  THIS IS AN INVALIDATING OPERATION */

static inline ulong
fd_pod_alloc_subpod( uchar      * FD_RESTRICT pod,
                     char const * FD_RESTRICT path,
                     ulong                    max ) { /* Assumes max>=FD_POD_FOOTPRINT_MIN */
  ulong off = fd_pod_alloc( pod, path, FD_POD_VAL_TYPE_SUBPOD, fd_pod_footprint( max ) );
  if( FD_UNLIKELY( !off ) ) return 0UL;
  fd_pod_new( pod + off, max );
  return off;
}

/* fd_pod_alloc_buf creates a empty buffer at path with space for up to
   val_sz bytes in the given pod.  Returns offset of buf on success
   (e.g. pod + off is the location of first byte of buf) and 0 on
   failure.  This offset is valid for the pod's lifetime or an
   invalidating operation is done on the pod.

   IMPORTANT!  THIS IS AN INVALIDATING OPERATION */

static inline ulong
fd_pod_alloc_buf( uchar      * FD_RESTRICT pod,
                  char const * FD_RESTRICT path,
                  ulong                    val_sz ) { /* Bound of final buffer sz */
  return fd_pod_alloc( pod, path, FD_POD_VAL_TYPE_BUF, val_sz );
}

/* fd_pod_alloc_cstr creates a space for cstr value at path with space
   for up to val_sz bytes (including terminating '\0').  Returns offset
   of cstr on success (e.g. pod + off is the location of first byte of
   cstr) and 0 on failure.  val_sz of 0 indicates that val is the NULL
   pointer.  This offset is valid for the pod's lifetime or an
   invalidating operation is done on the pod.

   IMPORTANT!  THIS IS AN INVALIDATING OPERATION */

static inline ulong
fd_pod_alloc_cstr( uchar      * FD_RESTRICT pod,
                   char const * FD_RESTRICT path,
                   ulong                    val_sz ) { /* Bound of final length of cstr, including terminating '\0' */
  return fd_pod_alloc( pod, path, FD_POD_VAL_TYPE_CSTR, val_sz );
}

/* Specific insert APIs ***********************************************/

/* fd_pod_insert_subpod inserts the subpod into the pod at the given
   path.  It is up to the user to do compaction of the subpod and/or
   overall pod as desired.  Returns offset where subpod inserted, 0 on
   failure.  This offset is valid for the pod's lifetime or an
   invalidating operation is done on the pod.

   IMPORTANT!  THIS IS AN INVALIDATING OPERATION */

static inline ulong
fd_pod_insert_subpod( uchar       * FD_RESTRICT pod,
                      char const  * FD_RESTRICT path,
                      uchar const * FD_RESTRICT subpod ) {
  return fd_pod_insert( pod, path, FD_POD_VAL_TYPE_SUBPOD, fd_pod_max( subpod ), subpod );
}

/* fd_pod_insert_buf inserts the size val_sz buffer val into the pod
   at the given path.  Returns offset where subpod inserted, 0 on
   failure.  This offset is valid for the pod's lifetime or an
   invalidating operation is done on the pod.

   IMPORTANT!  THIS IS AN INVALIDATING OPERATION */

static inline ulong
fd_pod_insert_buf( uchar      * FD_RESTRICT pod,
                   char const * FD_RESTRICT path,
                   void       * FD_RESTRICT val,
                   ulong                    val_sz ) {
  return fd_pod_insert( pod, path, FD_POD_VAL_TYPE_BUF, val_sz, val );
}

/* fd_pod_insert_cstr inserts the cstr val into the pod at the given
   path.  It is fine to insert NULL for val and/or the empty string
   (they will be recovered as such too).  Returns offset where cstr
   inserted, 0 on failure.  This offset is valid for the pod's lifetime
   or an invalidating operation is done on the pod.

   IMPORTANT!  THIS IS AN INVALIDATING OPERATION */

static inline ulong
fd_pod_insert_cstr( uchar      * FD_RESTRICT pod,
                    char const * FD_RESTRICT path,
                    char const * FD_RESTRICT val ) {
  return fd_pod_insert( pod, path, FD_POD_VAL_TYPE_CSTR, val ? (strlen( val ) + 1UL) : 0UL, val );
}

/* fd_pod_insert_[type] inserts the [type] val into the pod at the given
   path.  Returns offset where val was inserted, 0 on failure.  The
   inserted representation might be compressed.  This offset is valid
   for the pod's lifetime or an invalidating operation is done on the
   pod.

   IMPORTANT!  THIS IS AN INVALIDATING OPERATION */

#define FD_POD_IMPL(type,TYPE)                                                   \
static inline ulong /* offset where val stored in pod, 0 on failure */           \
fd_pod_insert_##type( uchar      * FD_RESTRICT pod,                              \
                      char const * FD_RESTRICT path,                             \
                      type                     val ) {                           \
  return fd_pod_insert( pod, path, FD_POD_VAL_TYPE_##TYPE, sizeof(type), &val ); \
}

FD_POD_IMPL( char,    CHAR    )
FD_POD_IMPL( schar,   SCHAR   )
FD_POD_IMPL( uchar,   UCHAR   )
FD_POD_IMPL( float,   FLOAT   )
#if FD_HAS_DOUBLE
FD_POD_IMPL( double,  DOUBLE  )
#endif

#undef FD_POD_IMPL

#define FD_POD_IMPL(type,TYPE)                                           \
static inline ulong                                                      \
fd_pod_insert_##type( uchar      * FD_RESTRICT pod,                      \
                      char const * FD_RESTRICT path,                     \
                      type                     val ) {                   \
  ulong val_sz = fd_ulong_svw_enc_sz( (ulong)val );                      \
  ulong off = fd_pod_alloc( pod, path, FD_POD_VAL_TYPE_##TYPE, val_sz ); \
  if( FD_UNLIKELY( !off ) ) return 0UL;                                  \
  fd_ulong_svw_enc( pod + off, (ulong)val );                             \
  return off;                                                            \
}

FD_POD_IMPL( ushort, USHORT )
FD_POD_IMPL( uint,   UINT   )
FD_POD_IMPL( ulong,  ULONG  )

#undef FD_POD_IMPL

#define FD_POD_IMPL(type,TYPE)                                           \
static inline ulong                                                      \
fd_pod_insert_##type( uchar      * FD_RESTRICT pod,                      \
                      char const * FD_RESTRICT path,                     \
                      type                     val ) {                   \
  ulong zz_val = fd_long_zz_enc( (long)val );                            \
  ulong val_sz = fd_ulong_svw_enc_sz( zz_val );                          \
  ulong off = fd_pod_alloc( pod, path, FD_POD_VAL_TYPE_##TYPE, val_sz ); \
  if( FD_UNLIKELY( !off ) ) return 0UL;                                  \
  fd_ulong_svw_enc( pod + off, zz_val );                                 \
  return off;                                                            \
}

FD_POD_IMPL( short, SHORT )
FD_POD_IMPL( int,   INT   )
FD_POD_IMPL( long,  LONG  )

#undef FD_POD_IMPL

#if FD_HAS_INT128
static inline ulong
fd_pod_insert_uint128( uchar      * FD_RESTRICT pod,
                       char const * FD_RESTRICT path,
                       uint128                  val ) {
  ulong lo     = (ulong) val;
  ulong hi     = (ulong)(val>>64);
  ulong val_sz = fd_ulong_svw_enc_sz( lo ) + fd_ulong_svw_enc_sz( hi );
  ulong off    = fd_pod_alloc( pod, path, FD_POD_VAL_TYPE_UINT128, val_sz );
  if( FD_UNLIKELY( !off ) ) return 0UL;
  fd_ulong_svw_enc( fd_ulong_svw_enc( pod + off, lo ), hi );
  return off;
}

static inline ulong
fd_pod_insert_int128( uchar      * FD_RESTRICT pod,
                      char const * FD_RESTRICT path,
                      int128                   val ) {
  uint128 zz_val   = fd_int128_zz_enc( val );
  ulong   lo       = (ulong) zz_val;
  ulong   hi       = (ulong)(zz_val>>64);
  ulong   val_sz   = fd_ulong_svw_enc_sz( lo ) + fd_ulong_svw_enc_sz( hi );
  ulong   off      = fd_pod_alloc( pod, path, FD_POD_VAL_TYPE_INT128, val_sz );
  if( FD_UNLIKELY( !off ) ) return 0UL;
  fd_ulong_svw_enc( fd_ulong_svw_enc( pod + off, lo ), hi );
  return off;
}
#endif

/* Specific query APIs ************************************************/

/* fd_pod_query_subpod queries for the subpod in pod at path.  Returns
   a pointer to the pod in the local address space on success or NULL on
   failure.  The return pointer's lifetime is the pod's local join
   lifetime or an invalidating operation is done on the pod. */

FD_FN_PURE static inline uchar const *
fd_pod_query_subpod( uchar const * FD_RESTRICT pod,
                     char const  * FD_RESTRICT path ) {
  fd_pod_info_t info[1];
  if( FD_UNLIKELY( fd_pod_query( pod, path, info )        ) ||
      FD_UNLIKELY( info->val_type!=FD_POD_VAL_TYPE_SUBPOD ) ) return NULL;
  return (uchar const *)info->val;
}

/* fd_pod_query_buf queries for the buffer in pod at path.  Returns the
   pointer to the buffer in the local address space on success or NULL
   on failure.  On success, if opt_buf_sz is non-NULL, *opt_buf_sz will
   have the size of the buffer in bytes on return.  *opt_buf_sz is
   untouched otherwise.  The return pointer's lifetime is the pod's
   local join lifetime or an invalidating operation is done on the pod. */

FD_FN_PURE static inline void const *
fd_pod_query_buf( uchar const * FD_RESTRICT pod,
                  char const  * FD_RESTRICT path,
                  ulong       * FD_RESTRICT opt_buf_sz ) {
  fd_pod_info_t info[1];
  if( FD_UNLIKELY( fd_pod_query( pod, path, info )     ) ||
      FD_UNLIKELY( info->val_type!=FD_POD_VAL_TYPE_BUF ) ) return NULL;
  if( opt_buf_sz ) *opt_buf_sz = info->val_sz;
  return info->val;
}

/* fd_pod_query_cstr queries for the cstr in pod at path.  Returns the
   pointer to the cstr in the local address on success or def on
   failure.  The return pointer's lifetime is the pod's local join
   lifetime or an invalidating operation is done on the pod. */

FD_FN_UNUSED FD_FN_PURE static char const * /* Work around -Winline */
fd_pod_query_cstr( uchar const * FD_RESTRICT pod,
                   char const  * FD_RESTRICT path,
                   char const  * FD_RESTRICT def ) {
  fd_pod_info_t info[1];
  if( FD_UNLIKELY( fd_pod_query( pod, path, info )      ) ||
      FD_UNLIKELY( info->val_type!=FD_POD_VAL_TYPE_CSTR ) ) return def;
  return info->val_sz ? info->val : NULL;
}

/* fd_pod_query_[type] queries for the [type] in pod at path.  Returns
   the query result on success or def on failure. */

#define FD_POD_IMPL(type,TYPE)                                            \
FD_FN_UNUSED FD_FN_PURE static type /* Work around -Winline */            \
fd_pod_query_##type( uchar const * FD_RESTRICT pod,                       \
                     char const  * FD_RESTRICT path,                      \
                     type                      def ) {                    \
  fd_pod_info_t info[1];                                                  \
  if( FD_UNLIKELY( fd_pod_query( pod, path, info )        ) ||            \
      FD_UNLIKELY( info->val_type!=FD_POD_VAL_TYPE_##TYPE ) ) return def; \
  return *(type const *)(info->val);                                      \
}

FD_POD_IMPL( char,   CHAR   )
FD_POD_IMPL( schar,  SCHAR  )
FD_POD_IMPL( uchar,  UCHAR  )
FD_POD_IMPL( float,  FLOAT  )
#if FD_HAS_DOUBLE
FD_POD_IMPL( double, DOUBLE )
#endif

#undef FD_POD_IMPL

#define FD_POD_IMPL(type,TYPE)                                            \
FD_FN_UNUSED FD_FN_PURE static type /* Work around -Winline */            \
fd_pod_query_##type( uchar const * FD_RESTRICT pod,                       \
                     char const  * FD_RESTRICT path,                      \
                     type                      def ) {                    \
  fd_pod_info_t info[1];                                                  \
  if( FD_UNLIKELY( fd_pod_query( pod, path, info )        ) ||            \
      FD_UNLIKELY( info->val_type!=FD_POD_VAL_TYPE_##TYPE ) ) return def; \
  ulong u; fd_ulong_svw_dec( info->val, &u );                             \
  return (type)u;                                                         \
}

FD_POD_IMPL( ushort, USHORT )
FD_POD_IMPL( uint,   UINT   )
FD_POD_IMPL( ulong,  ULONG  )

#undef FD_POD_IMPL

#define FD_POD_IMPL(type,TYPE)                                            \
FD_FN_UNUSED FD_FN_PURE static type /* Work around -Winline */            \
fd_pod_query_##type( uchar const * FD_RESTRICT pod,                       \
                     char const  * FD_RESTRICT path,                      \
                     type                      def ) {                    \
  fd_pod_info_t info[1];                                                  \
  if( FD_UNLIKELY( fd_pod_query( pod, path, info )        ) ||            \
      FD_UNLIKELY( info->val_type!=FD_POD_VAL_TYPE_##TYPE ) ) return def; \
  ulong u; fd_ulong_svw_dec( info->val, &u );                             \
  return (type)fd_long_zz_dec( u );                                       \
}

FD_POD_IMPL( short, SHORT )
FD_POD_IMPL( int,   INT   )
FD_POD_IMPL( long,  LONG  )

#undef FD_POD_IMPL

#if FD_HAS_INT128
FD_FN_UNUSED FD_FN_PURE static uint128 /* Work around -Winline */
fd_pod_query_uint128( uchar const * FD_RESTRICT pod,
                      char const  * FD_RESTRICT path,
                      uint128                   def ) {
  fd_pod_info_t info[1];
  if( FD_UNLIKELY( fd_pod_query( pod, path, info )         ) ||
      FD_UNLIKELY( info->val_type!=FD_POD_VAL_TYPE_UINT128 ) ) return def;
  union { ulong w[2]; uint128 u; } tmp;
  fd_ulong_svw_dec( fd_ulong_svw_dec( (uchar const *)info->val, tmp.w ), tmp.w+1 );
  return tmp.u;
}

FD_FN_UNUSED FD_FN_PURE static int128 /* Work around -Winline */
fd_pod_query_int128( uchar const * FD_RESTRICT pod,
                     char const  * FD_RESTRICT path,
                     int128                    def ) {
  fd_pod_info_t info[1];
  if( FD_UNLIKELY( fd_pod_query( pod, path, info )        ) ||
      FD_UNLIKELY( info->val_type!=FD_POD_VAL_TYPE_INT128 ) ) return def;
  union { ulong w[2]; uint128 u; } tmp;
  fd_ulong_svw_dec( fd_ulong_svw_dec( (uchar const *)info->val, tmp.w ), tmp.w+1 );
  return fd_int128_zz_dec( tmp.u );
}
#endif

FD_PROTOTYPES_END

#endif /* HEADER_fd_src_pod_fd_pod_h */
