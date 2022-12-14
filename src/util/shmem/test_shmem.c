#include "../fd_util.h"

#if FD_HAS_HOSTED && FD_HAS_X86

#include <ctype.h> /* For isalnum */
#include <errno.h>

FD_STATIC_ASSERT( FD_SHMEM_JOIN_MAX>0UL, unit_test );

FD_STATIC_ASSERT( FD_SHMEM_JOIN_MODE_READ_ONLY ==0, unit_test );
FD_STATIC_ASSERT( FD_SHMEM_JOIN_MODE_READ_WRITE==1, unit_test );

FD_STATIC_ASSERT( FD_SHMEM_NUMA_MAX> 0L,                unit_test );
FD_STATIC_ASSERT( FD_SHMEM_CPU_MAX >=FD_SHMEM_NUMA_MAX, unit_test );

FD_STATIC_ASSERT( FD_SHMEM_UNKNOWN_LG_PAGE_SZ ==-1, unit_test );
FD_STATIC_ASSERT( FD_SHMEM_NORMAL_LG_PAGE_SZ  ==12, unit_test );
FD_STATIC_ASSERT( FD_SHMEM_HUGE_LG_PAGE_SZ    ==21, unit_test );
FD_STATIC_ASSERT( FD_SHMEM_GIGANTIC_LG_PAGE_SZ==30, unit_test );

FD_STATIC_ASSERT( FD_SHMEM_UNKNOWN_PAGE_SZ == 0UL,                                unit_test );
FD_STATIC_ASSERT( FD_SHMEM_NORMAL_PAGE_SZ  ==(1UL<<FD_SHMEM_NORMAL_LG_PAGE_SZ  ), unit_test );
FD_STATIC_ASSERT( FD_SHMEM_HUGE_PAGE_SZ    ==(1UL<<FD_SHMEM_HUGE_LG_PAGE_SZ    ), unit_test );
FD_STATIC_ASSERT( FD_SHMEM_GIGANTIC_PAGE_SZ==(1UL<<FD_SHMEM_GIGANTIC_LG_PAGE_SZ), unit_test );

FD_STATIC_ASSERT( FD_SHMEM_NAME_MAX==FD_LOG_NAME_MAX, unit_test );

FD_STATIC_ASSERT( FD_SHMEM_PAGE_SZ_CSTR_MAX==9UL, unit_test );

int
main( int     argc,
      char ** argv ) {
  fd_boot( &argc, &argv );

# define TEST(c) do if( !(c) ) { FD_LOG_WARNING(( "FAIL: " #c )); return 1; } while(0)

  fd_rng_t _rng[1]; fd_rng_t * rng = fd_rng_join( fd_rng_new( _rng, 0U, 0UL ) );
  
  ulong numa_cnt = fd_shmem_numa_cnt(); TEST( (1UL<=numa_cnt) & (numa_cnt<=FD_SHMEM_NUMA_MAX) );
  ulong cpu_cnt  = fd_shmem_cpu_cnt (); TEST( (1UL<=cpu_cnt ) & (cpu_cnt <=FD_SHMEM_CPU_MAX ) );
  TEST( numa_cnt<=cpu_cnt );

  TEST( fd_shmem_numa_idx( cpu_cnt )==ULONG_MAX );
  for( ulong cpu_idx=0UL; cpu_idx<cpu_cnt; cpu_idx++ ) {
    ulong numa_idx = fd_shmem_numa_idx( cpu_idx );
    TEST( numa_idx<numa_cnt );
    FD_LOG_NOTICE(( "cpu %lu -> numa %lu", cpu_idx, numa_idx ));
  }

  TEST( !fd_shmem_name_len( NULL ) ); /* NULL name */

  for( int i=0; i<1000000; i++ ) {
    ulong len = (ulong)fd_rng_uint_roll( rng, FD_SHMEM_NAME_MAX+1UL ); /* In [0,FD_SHMEM_NAME_MAX] */
    char name[ FD_SHMEM_NAME_MAX+1UL ];
    for( ulong b=0UL; b<len; b++ ) {
      uint r = fd_rng_uint_roll( rng, 66U ); /* In [0,65] */
      char c;
      if     ( r< 26U ) c = (char)( ((uint)'A') +  r      ); /* Allowed anywhere, A-Z */
      else if( r< 52U ) c = (char)( ((uint)'a') + (r-26U) ); /* Allowed anywhere, a-z */
      else if( r< 62U ) c = (char)( ((uint)'0') + (r-52U) ); /* Allowed anywhere, 0-9 */
      else if( r==62U ) c = '.'; /* Forbidden at start */
      else if( r==63U ) c = '-'; /* " */
      else if( r==64U ) c = '_'; /* " */
      else              c = '@'; /* Completely forbidden */
      name[b] = c;
    }
    name[len] = '\0';

    ulong expected = len;
    if     ( len< 1UL               ) expected = 0UL; /* too short */
    else if( len>=FD_SHMEM_NAME_MAX ) expected = 0UL; /* too long */
    else if( !isalnum( name[0] )    ) expected = 0UL; /* invalid first character */
    else
      for( ulong b=1UL; b<len; b++ ) {
        char c = name[b];
        if( !( isalnum( c ) || (c=='_') || (c=='-') || (c=='.') ) ) { expected = 0UL; break; } /* invalid suffix character */
      }

    TEST( fd_shmem_name_len( name )==expected );
  }

  TEST( fd_shmem_name_len( ""                                         )==0UL                     ); /* too short */
  TEST( fd_shmem_name_len( "1"                                        )==1UL                     );
  TEST( fd_shmem_name_len( "-"                                        )==0UL                     ); /* bad first char */
  TEST( fd_shmem_name_len( "123456789012345678901234567890123456789"  )==(FD_SHMEM_NAME_MAX-1UL) );
  TEST( fd_shmem_name_len( "1234567890123456789012345678901234567890" )==0UL                     ); /* too long */

  TEST( fd_cstr_to_shmem_lg_page_sz( NULL       )==FD_SHMEM_UNKNOWN_LG_PAGE_SZ  );
  TEST( fd_cstr_to_shmem_lg_page_sz( ""         )==FD_SHMEM_UNKNOWN_LG_PAGE_SZ  );
  TEST( fd_cstr_to_shmem_lg_page_sz( "1"        )==FD_SHMEM_UNKNOWN_LG_PAGE_SZ  );
  TEST( fd_cstr_to_shmem_lg_page_sz( "foo"      )==FD_SHMEM_UNKNOWN_LG_PAGE_SZ  );
  TEST( fd_cstr_to_shmem_lg_page_sz( "normal"   )==FD_SHMEM_NORMAL_LG_PAGE_SZ   );
  TEST( fd_cstr_to_shmem_lg_page_sz( "NORMAL"   )==FD_SHMEM_NORMAL_LG_PAGE_SZ   );
  TEST( fd_cstr_to_shmem_lg_page_sz( "12"       )==FD_SHMEM_NORMAL_LG_PAGE_SZ   );
  TEST( fd_cstr_to_shmem_lg_page_sz( "huge"     )==FD_SHMEM_HUGE_LG_PAGE_SZ     );
  TEST( fd_cstr_to_shmem_lg_page_sz( "HUGE"     )==FD_SHMEM_HUGE_LG_PAGE_SZ     );
  TEST( fd_cstr_to_shmem_lg_page_sz( "21"       )==FD_SHMEM_HUGE_LG_PAGE_SZ     );
  TEST( fd_cstr_to_shmem_lg_page_sz( "gigantic" )==FD_SHMEM_GIGANTIC_LG_PAGE_SZ );
  TEST( fd_cstr_to_shmem_lg_page_sz( "GIGANTIC" )==FD_SHMEM_GIGANTIC_LG_PAGE_SZ );
  TEST( fd_cstr_to_shmem_lg_page_sz( "30"       )==FD_SHMEM_GIGANTIC_LG_PAGE_SZ );

  TEST( !strcmp( fd_shmem_lg_page_sz_to_cstr(  0 ), "unknown"  ) );
  TEST( !strcmp( fd_shmem_lg_page_sz_to_cstr( 12 ), "normal"   ) );
  TEST( !strcmp( fd_shmem_lg_page_sz_to_cstr( 21 ), "huge"     ) );
  TEST( !strcmp( fd_shmem_lg_page_sz_to_cstr( 30 ), "gigantic" ) );

  TEST( fd_cstr_to_shmem_page_sz( NULL         )==FD_SHMEM_UNKNOWN_PAGE_SZ  );
  TEST( fd_cstr_to_shmem_page_sz( ""           )==FD_SHMEM_UNKNOWN_PAGE_SZ  );
  TEST( fd_cstr_to_shmem_page_sz( "1"          )==FD_SHMEM_UNKNOWN_PAGE_SZ  );
  TEST( fd_cstr_to_shmem_page_sz( "foo"        )==FD_SHMEM_UNKNOWN_PAGE_SZ  );
  TEST( fd_cstr_to_shmem_page_sz( "normal"     )==FD_SHMEM_NORMAL_PAGE_SZ   );
  TEST( fd_cstr_to_shmem_page_sz( "NORMAL"     )==FD_SHMEM_NORMAL_PAGE_SZ   );
  TEST( fd_cstr_to_shmem_page_sz( "4096"       )==FD_SHMEM_NORMAL_PAGE_SZ   );
  TEST( fd_cstr_to_shmem_page_sz( "huge"       )==FD_SHMEM_HUGE_PAGE_SZ     );
  TEST( fd_cstr_to_shmem_page_sz( "HUGE"       )==FD_SHMEM_HUGE_PAGE_SZ     );
  TEST( fd_cstr_to_shmem_page_sz( "2097152"    )==FD_SHMEM_HUGE_PAGE_SZ     );
  TEST( fd_cstr_to_shmem_page_sz( "gigantic"   )==FD_SHMEM_GIGANTIC_PAGE_SZ );
  TEST( fd_cstr_to_shmem_page_sz( "GIGANTIC"   )==FD_SHMEM_GIGANTIC_PAGE_SZ );
  TEST( fd_cstr_to_shmem_page_sz( "1073741824" )==FD_SHMEM_GIGANTIC_PAGE_SZ );

  TEST( !strcmp( fd_shmem_page_sz_to_cstr(          0UL ), "unknown"  ) );
  TEST( !strcmp( fd_shmem_page_sz_to_cstr(       4096UL ), "normal"   ) );
  TEST( !strcmp( fd_shmem_page_sz_to_cstr(    2097152UL ), "huge"     ) );
  TEST( !strcmp( fd_shmem_page_sz_to_cstr( 1073741824UL ), "gigantic" ) );

  fd_shmem_join_info_t info[ 1 ];

  /* These should all fail */
  /* FIXME: COVERAGE OF LEAVE WITH NO JOIN BEHAVIOR */
  /* FIXME: COVERAGE OF JOIN/LEAVE FUNCS AND JOIN OPT_INFO */

  TEST( fd_shmem_join_query_by_name( NULL, NULL )==EINVAL ); TEST( fd_shmem_join_query_by_name( NULL, info )==EINVAL );
  TEST( fd_shmem_join_query_by_join( NULL, NULL )==EINVAL ); TEST( fd_shmem_join_query_by_join( NULL, info )==EINVAL );
  TEST( fd_shmem_join_query_by_addr( NULL, NULL )==EINVAL ); TEST( fd_shmem_join_query_by_addr( NULL, info )==EINVAL );
  TEST( fd_shmem_join_query_by_name( "",   NULL )==EINVAL ); TEST( fd_shmem_join_query_by_name( "",   info )==EINVAL );

  if( argc>1 ) {
    ulong name_cnt = fd_ulong_min( (ulong)(argc-1), FD_SHMEM_JOIN_MAX );
    char ** _name = &argv[1]; /* Assumed valid and distinct */

    fd_shmem_join_info_t ref_info[ FD_SHMEM_JOIN_MAX ];
    fd_memset( ref_info, 0, name_cnt*sizeof(fd_shmem_join_info_t) );

    for( int i=0; i<65536; i++ ) {
      ulong idx = fd_rng_ulong_roll( rng, name_cnt );
      char const * name = _name[ idx ];

      uint r  = fd_rng_uint( rng );
      int  op = (int)(r & 1U); r >>= 1;
      int  rw = (int)(r & 1U); r >>= 1;

      if( op ) { /* join */

        int mode = rw ? FD_SHMEM_JOIN_MODE_READ_WRITE : FD_SHMEM_JOIN_MODE_READ_ONLY;
        if( !ref_info[ idx ].ref_cnt ) { /* this join needs to map it */

          TEST( fd_shmem_join_query_by_name( name, NULL )==ENOENT );
          TEST( fd_shmem_join_query_by_name( name, info )==ENOENT );

          void * join = fd_shmem_join( name, mode, NULL, NULL, NULL );
          TEST( join );

          TEST( !fd_shmem_join_query_by_name( name, NULL ) );
          TEST( !fd_shmem_join_query_by_name( name, info ) );

          void * shmem    = info->shmem;
          ulong  page_sz  = info->page_sz;
          ulong  page_cnt = info->page_cnt;
          ulong  sz       = page_sz*page_cnt;
          ulong  off      = fd_rng_ulong_roll( rng, sz );

          TEST( info->ref_cnt==1L                                                  );
          TEST( info->join   ==join                                                );
          TEST( shmem        ==join                                                );
          TEST( fd_ulong_is_aligned( (ulong)shmem, page_sz )                       );
          TEST( fd_shmem_is_page_sz( page_sz )                                     );
          TEST( page_cnt     > 0UL                                                 );
          TEST( page_cnt     <=((ulong)LONG_MAX/page_sz)                           );
          TEST( info->mode   ==mode                                                );
          TEST( info->hash   ==(uint)fd_hash( 0UL, info->name, FD_SHMEM_NAME_MAX ) );
          TEST( !strcmp( info->name, name )                                        );

          fd_shmem_join_info_t * ref = &ref_info[idx];

          fd_memset( ref, 0, sizeof(fd_shmem_join_info_t) );
          TEST( !fd_shmem_join_query_by_join( join, NULL ) );
          TEST( !fd_shmem_join_query_by_join( join, ref ) );
          TEST( !memcmp( ref, info, sizeof(fd_shmem_join_info_t) ) );

          fd_memset( ref, 0, sizeof(fd_shmem_join_info_t) );
          TEST( !fd_shmem_join_query_by_addr( ((uchar *)shmem) + off, NULL ) );
          TEST( !fd_shmem_join_query_by_addr( ((uchar *)shmem) + off, ref  ) );
          TEST( !memcmp( ref, info, sizeof(fd_shmem_join_info_t) ) );

        } else { /* this join just increments the ref cnt */

          fd_shmem_join_info_t * ref = &ref_info[idx];
          void * join     = ref->join;
          void * shmem    = ref->shmem;
          ulong  page_sz  = ref->page_sz;
          ulong  page_cnt = ref->page_cnt;
          ulong  sz       = page_sz*page_cnt;
          ulong  off      = fd_rng_ulong_roll( rng, sz );

          fd_memset( info, 0, sizeof(fd_shmem_join_info_t) );
          TEST( !fd_shmem_join_query_by_name( name, NULL ) );
          TEST( !fd_shmem_join_query_by_name( name, info ) );
          TEST( !memcmp( info, ref, sizeof(fd_shmem_join_info_t) ) );

          fd_memset( info, 0, sizeof(fd_shmem_join_info_t) );
          TEST( !fd_shmem_join_query_by_join( join, NULL ) );
          TEST( !fd_shmem_join_query_by_join( join, info ) );
          TEST( !memcmp( info, ref, sizeof(fd_shmem_join_info_t) ) );

          fd_memset( info, 0, sizeof(fd_shmem_join_info_t) );
          TEST( !fd_shmem_join_query_by_addr( ((uchar *)shmem) + off, NULL ) );
          TEST( !fd_shmem_join_query_by_addr( ((uchar *)shmem) + off, info ) );
          TEST( !memcmp( info, ref, sizeof(fd_shmem_join_info_t) ) );

          TEST( fd_shmem_join( name, mode, NULL, NULL, NULL )==join );
          ref_info[idx].ref_cnt++;

          fd_memset( info, 0, sizeof(fd_shmem_join_info_t) );
          TEST( !fd_shmem_join_query_by_name( name, NULL ) );
          TEST( !fd_shmem_join_query_by_name( name, info ) );
          TEST( !memcmp( info, ref, sizeof(fd_shmem_join_info_t) ) );

          fd_memset( info, 0, sizeof(fd_shmem_join_info_t) );
          TEST( !fd_shmem_join_query_by_join( join, NULL ) );
          TEST( !fd_shmem_join_query_by_join( join, info ) );
          TEST( !memcmp( info, ref, sizeof(fd_shmem_join_info_t) ) );

          fd_memset( info, 0, sizeof(fd_shmem_join_info_t) );
          TEST( !fd_shmem_join_query_by_addr( ((uchar *)shmem) + off, NULL ) );
          TEST( !fd_shmem_join_query_by_addr( ((uchar *)shmem) + off, info ) );
          TEST( !memcmp( info, ref, sizeof(fd_shmem_join_info_t) ) );
        }

      } else { /* leave */

        if( ref_info[idx].ref_cnt<1L ) continue; /* Not currently joined */

        fd_shmem_join_info_t * ref = &ref_info[idx];
        void * join     = ref->join;
        void * shmem    = ref->shmem;
        ulong  page_sz  = ref->page_sz;
        ulong  page_cnt = ref->page_cnt;
        ulong  sz       = page_sz*page_cnt;
        ulong  off      = fd_rng_ulong_roll( rng, sz );

        fd_memset( info, 0, sizeof(fd_shmem_join_info_t) );
        TEST( !fd_shmem_join_query_by_name( name, NULL ) );
        TEST( !fd_shmem_join_query_by_name( name, info ) );
        TEST( !memcmp( info, ref, sizeof(fd_shmem_join_info_t) ) );

        fd_memset( info, 0, sizeof(fd_shmem_join_info_t) );
        TEST( !fd_shmem_join_query_by_join( join, NULL ) );
        TEST( !fd_shmem_join_query_by_join( join, info ) );
        TEST( !memcmp( info, ref, sizeof(fd_shmem_join_info_t) ) );

        fd_memset( info, 0, sizeof(fd_shmem_join_info_t) );
        TEST( !fd_shmem_join_query_by_addr( ((uchar *)shmem) + off, NULL ) );
        TEST( !fd_shmem_join_query_by_addr( ((uchar *)shmem) + off, info ) );
        TEST( !memcmp( info, ref, sizeof(fd_shmem_join_info_t) ) );

        fd_shmem_leave( join, NULL, NULL );
        ref_info[idx].ref_cnt--;

        if( !ref_info[idx].ref_cnt ) { /* this leave should have unmapped it */

          TEST( fd_shmem_join_query_by_name( name, NULL )==ENOENT );
          TEST( fd_shmem_join_query_by_name( name, info )==ENOENT );

          TEST( fd_shmem_join_query_by_join( join, NULL )==ENOENT );
          TEST( fd_shmem_join_query_by_join( join, info )==ENOENT );

          TEST( fd_shmem_join_query_by_addr( ((uchar *)shmem) + off, NULL )==ENOENT );
          TEST( fd_shmem_join_query_by_addr( ((uchar *)shmem) + off, info )==ENOENT );

        } else if( ref_info[idx].ref_cnt>1L ) { /* this leave just decrements the ref cnt */

          fd_memset( info, 0, sizeof(fd_shmem_join_info_t) );
          TEST( !fd_shmem_join_query_by_name( name, NULL ) );
          TEST( !fd_shmem_join_query_by_name( name, info ) );
          TEST( !memcmp( info, ref, sizeof(fd_shmem_join_info_t) ) );

          fd_memset( info, 0, sizeof(fd_shmem_join_info_t) );
          TEST( !fd_shmem_join_query_by_join( join, NULL ) );
          TEST( !fd_shmem_join_query_by_join( join, info ) );
          TEST( !memcmp( info, ref, sizeof(fd_shmem_join_info_t) ) );

          fd_memset( info, 0, sizeof(fd_shmem_join_info_t) );
          TEST( !fd_shmem_join_query_by_addr( ((uchar *)shmem) + off, NULL ) );
          TEST( !fd_shmem_join_query_by_addr( ((uchar *)shmem) + off, info ) );
          TEST( !memcmp( info, ref, sizeof(fd_shmem_join_info_t) ) );

        }
      }
    }
  }

  /* FIXME: DO MORE EXTENSIVE TESTS OF ACQUIRE / RELEASE */

# define TEST_ACQ(psz,pcnt,cpu) do {                                \
    void * _page = fd_shmem_acquire( (psz), (pcnt), (cpu) );        \
    TEST( _page );                                                  \
    TEST( fd_ulong_is_aligned( (ulong)_page, (psz) ) );             \
    TEST( !fd_shmem_numa_validate( _page, (psz), (pcnt), (cpu) ) ); \
    fd_shmem_release( _page, (psz), (pcnt) );                       \
  } while(0)

  TEST_ACQ( FD_SHMEM_NORMAL_PAGE_SZ,   3UL, 0UL );
  TEST_ACQ( FD_SHMEM_HUGE_PAGE_SZ,     2UL, 0UL );
  TEST_ACQ( FD_SHMEM_GIGANTIC_PAGE_SZ, 1UL, 0UL );

# undef TEST_ACQ

  fd_rng_delete( fd_rng_leave( rng ) );

# undef TEST

  FD_LOG_NOTICE(( "pass" ));
  fd_halt();
  return 0;
}

#else

int
main( int     argc,
      char ** argv ) {
  fd_boot( &argc, &argv );
  FD_LOG_WARNING(( "skip: unit test requires FD_HAS_HOSTED and FD_HAS_X86 capabilities" ));
  fd_halt();
  return 0;
}

#endif

