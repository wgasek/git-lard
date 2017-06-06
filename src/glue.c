#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "git/cache.h"
#include "git/dir.h"
#include "git/git-compat-util.h"
#include "git/pathspec.h"
#include "git/revision.h"
#include "git/list-objects.h"
#include "git/submodule.h"

#include "glue.h"
#include "verify.h"

static char s_tmpbuf[4096];
static struct pathspec pathspec;
static int prefixlen;

const char* SetupGitDirectory()
{
    const char* prefix = setup_git_directory();
    prefixlen = prefix ? strlen( prefix ) : 0;
    gitmodules_config();
    return prefix;
}

const char* GetGitDir()
{
    const char* gitdir = getenv( GIT_DIR_ENVIRONMENT );
    if( gitdir ) return gitdir;
    char* cwd = xgetcwd();
    int len = strlen( cwd );
    sprintf( s_tmpbuf, "%s%s.git", cwd, len && cwd[len-1] != '/' ? "/" : "" );
    free( cwd );
    return s_tmpbuf;
}

const char* GetGitWorkTree()
{
    return get_git_work_tree();
}

void ParsePathspec( const char* prefix )
{
    parse_pathspec( &pathspec, 0, PATHSPEC_PREFER_CWD | PATHSPEC_STRIP_SUBMODULE_SLASH_CHEAP, prefix, NULL );
}

int CheckIfConfigKeyExists( const char* key )
{
    const char* tmp;
    return git_config_get_value( key, &tmp );
}

void SetConfigKey( const char* key, const char* val )
{
    git_config_set( key, val );
}

struct rev_info* NewRevInfo()
{
    struct rev_info* revs = (struct rev_info*)malloc( sizeof( struct rev_info ) );
    init_revisions( revs, NULL );
    return revs;
}

void AddRevHead( struct rev_info* revs )
{
    add_head_to_pending( revs );
}

void AddRevAll( struct rev_info* revs )
{
    const char* argv[2] = { "", "--all" };
    verify( setup_revisions( 2, argv, revs, NULL ) == 1 );
}

void PrepareRevWalk( struct rev_info* revs )
{
    verify( prepare_revision_walk( revs ) == 0 );
}

void FreeRevs( struct rev_info* revs )
{
    free( revs );
    reset_revision_walk();
}

struct commit* GetRevision( struct rev_info* revs )
{
    return get_revision( revs );
}

static void null_show_commit( struct commit* a, void* b ) {}
static void null_show_object( struct object* a, const char* b, void* c ) {}

static void show_fat_object( struct object* obj, const char* name, void* data )
{
    if( obj->type == OBJ_BLOB )
    {
        unsigned long size;
        struct object_info oi = { NULL };
        oi.sizep = &size;
        sha1_object_info_extended( obj->oid.hash, &oi, LOOKUP_REPLACE_OBJECT );
        if( size == GitFatMagic )
        {
            enum object_type type;
            void* ptr = read_sha1_file( obj->oid.hash, &type, &size );
            if( memcmp( ptr, "#$# git-fat ", 12 ) == 0 )
            {
                ((void(*)(char*))data)( ptr );
            }
            free( ptr );
        }
    }
}

void GetFatObjectsFromRevs( struct rev_info* revs, void(*cb)( char* ) )
{
    revs->blob_objects = 1;
    revs->tree_objects = 1;
    traverse_commit_list( revs, null_show_commit, show_fat_object, cb );
}

static void show_object( struct object* obj, const char* name, void* data )
{
    if( obj->type == OBJ_BLOB )
    {
        unsigned long size;
        struct object_info oi = { NULL };
        oi.sizep = &size;
        sha1_object_info_extended( obj->oid.hash, &oi, LOOKUP_REPLACE_OBJECT );
        ((void(*)(char*,size_t))data)( sha1_to_hex( obj->oid.hash ), size );
    }
}

void GetObjectsFromRevs( struct rev_info* revs, void(*cb)( char*, size_t ) )
{
    revs->blob_objects = 1;
    revs->tree_objects = 1;
    traverse_commit_list( revs, null_show_commit, show_object, cb );
}

void GetCommitList( struct rev_info* revs, void(*cb)( char* ) )
{
    struct commit* commit;
    while( ( commit = get_revision( revs ) ) != NULL )
    {
        ((void(*)(char*))cb)( sha1_to_hex( commit->object.oid.hash ) );
    }
}

int ReadCache()
{
    return read_cache();
}

void ListFiles( void(*cb)( const char* ) )
{
    const char* super_prefix = get_super_prefix();

    for( int i=0; i<active_nr; i++ )
    {
        const struct cache_entry* ce = active_cache[i];
        if( ce->ce_flags & CE_UPDATE ) continue;
        char buf[1024];
        char* ptr = buf;
        if( super_prefix )
        {
            size_t len = strlen( super_prefix );
            memcpy( ptr, super_prefix, len );
            ptr += len;
        }

        size_t len = strlen( ce->name );
        memcpy( ptr, ce->name, len );
        ptr += len;

        *ptr = '\0';

        static char* ps_matched;
        if( match_pathspec( &pathspec, buf, ptr-buf, 0, ps_matched, S_ISDIR( ce->ce_mode ) || S_ISGITLINK( ce->ce_mode ) ) )
        {
            printf( "%s\n", buf + prefixlen );
        }
    }
}
