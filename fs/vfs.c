// vfs.c - Virtual File System minimalista
#include "vfs.h"
#include "minix_fs.h"
#include <string.h>

// External memory functions
extern void *kmalloc(size_t size);
extern void kfree(void *ptr);

// Lista de filesystems registrados
static struct filesystem_type *filesystems = NULL;

// Root filesystem
static struct vfs_superblock *root_sb = NULL;
static struct vfs_inode *root_inode = NULL;

// ============================================================================
// REGISTRO DE FILESYSTEMS
// ============================================================================

int register_filesystem(struct filesystem_type *fs)
{
    if (!fs)
        return -1;

    fs->next = filesystems;
    filesystems = fs;
    return 0;
}

int unregister_filesystem(struct filesystem_type *fs)
{
    if (!fs)
        return -1;

    struct filesystem_type **p = &filesystems;
    while (*p)
    {
        if (*p == fs)
        {
            *p = fs->next;
            return 0;
        }
        p = &(*p)->next;
    }
    return -1;
}

// ============================================================================
// PATH LOOKUP
// ============================================================================

struct vfs_inode *vfs_path_lookup(const char *path)
{
    if (!path || !root_inode)
        return NULL;

    // Por simplicidad, solo soportamos root "/"
    if (strcmp(path, "/") == 0)
    {
        return root_inode;
    }

    // Para otros paths, usar el filesystem específico
    // TODO: Implementar lookup completo
    return NULL;
}

// ============================================================================
// VFS OPERATIONS
// ============================================================================

int vfs_init(void)
{
    // Inicializar lista de filesystems
    filesystems = NULL;
    root_sb = NULL;
    root_inode = NULL;

    return 0;
}

int vfs_mount(const char *dev, const char *mountpoint, const char *fstype)
{
    if (!fstype)
        return -1;

    // Buscar el tipo de filesystem
    struct filesystem_type *fs_type = filesystems;
    while (fs_type)
    {
        if (strcmp(fs_type->name, fstype) == 0)
        {
            break;
        }
        fs_type = fs_type->next;
    }

    if (!fs_type)
        return -1; // Filesystem no encontrado

    // Montar el filesystem
    return fs_type->mount(dev, mountpoint);
}

int vfs_open(const char *path, int flags, struct vfs_file **file)
{
    if (!path || !file)
        return -1;

    struct vfs_inode *inode = vfs_path_lookup(path);
    if (!inode)
        return -1;

    // Crear file descriptor
    *file = kmalloc(sizeof(struct vfs_file));
    if (!*file)
        return -1;

    (*file)->f_inode = inode;
    (*file)->f_pos = 0;
    (*file)->f_flags = flags;
    (*file)->private_data = NULL;

    // Llamar a open del filesystem específico
    if (inode->i_fop && inode->i_fop->open)
    {
        return inode->i_fop->open(inode, *file);
    }

    return 0;
}

int vfs_read(struct vfs_file *file, char *buf, size_t count)
{
    if (!file || !buf)
        return -1;

    if (file->f_inode->i_fop && file->f_inode->i_fop->read)
    {
        return file->f_inode->i_fop->read(file, buf, count);
    }

    return -1;
}

int vfs_write(struct vfs_file *file, const char *buf, size_t count)
{
    if (!file || !buf)
        return -1;

    if (file->f_inode->i_fop && file->f_inode->i_fop->write)
    {
        return file->f_inode->i_fop->write(file, buf, count);
    }

    return -1;
}

int vfs_close(struct vfs_file *file)
{
    if (!file)
        return -1;

    int ret = 0;
    if (file->f_inode->i_fop && file->f_inode->i_fop->close)
    {
        ret = file->f_inode->i_fop->close(file);
    }

    kfree(file);
    return ret;
}

// ============================================================================
// VFS WRAPPERS PARA SYSCALLS
// ============================================================================

int vfs_ls(const char *path)
{
    // Delegar al filesystem específico por ahora
    extern int minix_fs_ls(const char *path);
    return minix_fs_ls(path);
}

int vfs_mkdir(const char *path, int mode __attribute__((unused)))
{
    // Delegar al filesystem específico por ahora
    extern int minix_fs_mkdir(const char *path);
    return minix_fs_mkdir(path);
}

int vfs_unlink(const char *path)
{
    // Delegar al filesystem específico por ahora
    extern int minix_fs_rm(const char *path);
    return minix_fs_rm(path);
}

// ============================================================================
// MINIX FILESYSTEM INTEGRATION
// ============================================================================

// Operaciones de archivo para MINIX
static struct file_operations minix_file_ops = {
    .open = NULL,  // TODO: Implementar
    .read = NULL,  // TODO: Implementar
    .write = NULL, // TODO: Implementar
    .close = NULL, // TODO: Implementar
};

// Operaciones de inode para MINIX
static struct inode_operations minix_inode_ops = {
    .lookup = NULL, // TODO: Implementar
    .create = NULL, // TODO: Implementar
    .mkdir = NULL,  // TODO: Implementar
    .unlink = NULL, // TODO: Implementar
};

// Operaciones de superblock para MINIX
static struct super_operations minix_super_ops = {
    .read_inode = NULL,   // TODO: Implementar
    .write_inode = NULL,  // TODO: Implementar
    .delete_inode = NULL, // TODO: Implementar
};

// Mount function para MINIX
static int minix_mount(const char *dev_name __attribute__((unused)), const char *dir_name __attribute__((unused)))
{
    // Inicializar MINIX filesystem
    extern int minix_fs_init(void);
    int ret = minix_fs_init();
    if (ret != 0)
        return ret;

    // Crear superblock
    root_sb = kmalloc(sizeof(struct vfs_superblock));
    if (!root_sb)
        return -1;

    root_sb->s_op = &minix_super_ops;
    root_sb->s_type = NULL;    // Se asignará después
    root_sb->s_fs_info = NULL; // Datos específicos de MINIX

    // Crear root inode
    root_inode = kmalloc(sizeof(struct vfs_inode));
    if (!root_inode)
    {
        kfree(root_sb);
        return -1;
    }

    root_inode->i_ino = 1;               // Root inode
    root_inode->i_mode = 0755 | 0040000; // Directory
    root_inode->i_size = 1024;
    root_inode->i_op = &minix_inode_ops;
    root_inode->i_fop = &minix_file_ops;
    root_inode->i_sb = root_sb;
    root_inode->i_private = NULL;

    return 0;
}

// Tipo de filesystem MINIX
static struct filesystem_type minix_fs_type = {
    .name = "minix",
    .mount = minix_mount,
    .next = NULL,
};

// Inicializar VFS con MINIX
int vfs_init_with_minix(void)
{
    // Inicializar VFS
    int ret = vfs_init();
    if (ret != 0)
        return ret;

    // Registrar MINIX filesystem
    ret = register_filesystem(&minix_fs_type);
    if (ret != 0)
        return ret;

    // Montar root filesystem
    ret = vfs_mount("/dev/hda", "/", "minix");
    if (ret != 0)
        return ret;

    return 0;
}