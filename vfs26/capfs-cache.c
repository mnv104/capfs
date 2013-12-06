/*
 * cache for frequently used objects 
 * Murali Vilayannur vilayann@cse.psu.edu
 */
#include "capfs_kernel_config.h"
#include "capfs_linux.h"
#include "ll_capfs.h"
#include "capfs_proc.h"

/* a cache for capfs-inode objects (i.e. capfs inode private data) */
extern kmem_cache_t *capfs_inode_cache;

void capfs_inode_initialize(struct capfs_inode *inode)
{
    inode->handle = 0;
    inode->name   = NULL;
    inode->super  = NULL;
    return;
}

/* should be called by the destroy_inode callback routine */
void capfs_inode_finalize(struct capfs_inode *inode)
{
    inode->handle = 0;
    inode->name   = NULL;
    inode->super  = NULL;
}

static void capfs_inode_cache_ctor(
    void *new_capfs_inode,
    kmem_cache_t * cachep,
    unsigned long flags)
{
    struct capfs_inode *capfs_inode = (struct capfs_inode *)new_capfs_inode;

    if (flags & SLAB_CTOR_CONSTRUCTOR)
    {
        memset(capfs_inode, 0, sizeof(struct capfs_inode));

        capfs_inode_initialize(capfs_inode);

        /*
           inode_init_once is from 2.6.x's inode.c; it's normally run
           when an inode is allocated by the system's inode slab
           allocator.  we call it here since we're overloading the
           system's inode allocation with this routine, thus we have
           to init vfs inodes manually
        */
        inode_init_once(&capfs_inode->vfs_inode);
        capfs_inode->vfs_inode.i_version = 1;
    }
    else
    {
        PERROR("WARNING!! inode_ctor called without ctor flag\n");
    }
}

static void capfs_inode_cache_dtor(
    void *old_capfs_inode,
    kmem_cache_t * cachep,
    unsigned long flags)
{
    struct capfs_inode *capfs_inode = (struct capfs_inode *) old_capfs_inode;

    if (capfs_inode && capfs_inode->name)
    {
        kfree(capfs_inode->name);
        capfs_inode->name = NULL;
    }
}

void capfs_inode_cache_initialize(void)
{
    capfs_inode_cache = kmem_cache_create(
            "capfs_inode_cache", /* name */
            sizeof(struct capfs_inode), /* size */
            0, /* alignment */
            0, /* debug flags if any */ 
            capfs_inode_cache_ctor, /* constructor */
            capfs_inode_cache_dtor /* destructor */);

    if (!capfs_inode_cache)
    {
        PERROR("Cannot create capfs_inode_cache\n");
    }
}

void capfs_inode_cache_finalize(void)
{
    if (kmem_cache_destroy(capfs_inode_cache) != 0)
    {
        PERROR("Failed to destroy capfs_inode_cache\n");
    }
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
