#include <defs.h>
#include <string.h>
#include <vfs.h>
#include <proc.h>
#include <file.h>
#include <unistd.h>
#include <iobuf.h>
#include <inode.h>
#include <stat.h>
#include <dirent.h>
#include <error.h>
#include <assert.h>

#define MIN(x,y) (((x)<(y))?(x):(y))


#define testfd(fd)                          ((fd) >= 0 && (fd) < FS_STRUCT_NENTRY)

static struct file *
get_filemap(void) {
    struct fs_struct *fs_struct = current->fs_struct;
    assert(fs_struct != NULL);
    assert(fs_count(fs_struct) > 0);
    return fs_struct->filemap;
}

void
filemap_init(struct file *filemap) {
    int fd;
    struct file *file = filemap;
    for (fd = 0; fd < FS_STRUCT_NENTRY; fd ++, file ++) {
        atomic_set(&(file->open_count), 0);
        file->status = FD_NONE, file->fd = fd;
    }
}

static int
filemap_alloc(int fd, struct file **file_store) {
//	panic("debug");
    struct file *file = get_filemap();
    if (fd == NO_FD) {
        for (fd = 0; fd < FS_STRUCT_NENTRY; fd ++, file ++) {
            if (file->status == FD_NONE) {
                goto found;
            }
        }
        return -E_MAX_OPEN;
    }
    else {
        if (testfd(fd)) {
            file += fd;
            if (file->status == FD_NONE) {
                goto found;
            }
            return -E_BUSY;
        }
        return -E_INVAL;
    }
found:
    assert(fopen_count(file) == 0);
    file->status = FD_INIT, file->node = NULL;
    *file_store = file;
    return 0;
}

static void
filemap_free(struct file *file) {
    assert(file->status == FD_INIT || file->status == FD_CLOSED);
    assert(fopen_count(file) == 0);
    if (file->status == FD_CLOSED) {
        vfs_close(file->node);
    }
    file->status = FD_NONE;
}

static void
filemap_acquire(struct file *file) {
    assert(file->status == FD_OPENED);
    fopen_count_inc(file);
}

static void
filemap_release(struct file *file) {
    assert(file->status == FD_OPENED || file->status == FD_CLOSED);
    assert(fopen_count(file) > 0);
    if (fopen_count_dec(file) == 0) {
        filemap_free(file);
    }
}

void
filemap_open(struct file *file) {
    assert(file->status == FD_INIT && file->node != NULL);
    file->status = FD_OPENED;
    fopen_count_inc(file);
}

void
filemap_close(struct file *file) {
    assert(file->status == FD_OPENED);
    assert(fopen_count(file) > 0);
    file->status = FD_CLOSED;
    if (fopen_count_dec(file) == 0) {
        filemap_free(file);
    }
}

void
filemap_dup(struct file *to, struct file *from) {
	//kprintf("[filemap_dup]from fd=%d, to fd=%d\n",from->fd, to->fd);
    assert(to->status == FD_INIT && from->status == FD_OPENED);
    to->pos = from->pos;
    to->readable = from->readable;
    to->writable = from->writable;
    struct inode *node = from->node;
    vop_ref_inc(node), vop_open_inc(node);
    to->node = node;
    filemap_open(to);
}

static inline int
fd2file(int fd, struct file **file_store) {
    if (testfd(fd)) {
        struct file *file = get_filemap() + fd;
        if (file->status == FD_OPENED && file->fd == fd) {
            *file_store = file;
            return 0;
        }
    }
    return -E_INVAL;
}

bool
file_testfd(int fd, bool readable, bool writable) {
    int ret;
    struct file *file;
    if ((ret = fd2file(fd, &file)) != 0) {
        return 0;
    }
    if (readable && !file->readable) {
        return 0;
    }
    if (writable && !file->writable) {
        return 0;
    }
    return 1;
}

int
file_open(char *path, uint32_t open_flags) {
    bool readable = 0, writable = 0;
    switch (open_flags & O_ACCMODE) {
    case O_RDONLY: readable = 1; break;
    case O_WRONLY: writable = 1; break;
    case O_RDWR:
        readable = writable = 1;
        break;
    default:
        return -E_INVAL;
    }

    int ret;
    struct file *file;
    if ((ret = filemap_alloc(NO_FD, &file)) != 0) {
        return ret;
    }

    struct inode *node;
    if ((ret = vfs_open(path, open_flags, &node)) != 0) {
        filemap_free(file);
        return ret;
    }

    file->pos = 0;
    if (open_flags & O_APPEND) {
        struct stat __stat, *stat = &__stat;
        if ((ret = vop_fstat(node, stat)) != 0) {
            vfs_close(node);
            filemap_free(file);
            return ret;
        }
        file->pos = stat->st_size;
    }

    file->node = node;
    file->readable = readable;
    file->writable = writable;
    filemap_open(file);
    return file->fd;
}

int
file_close(int fd) {
    int ret;
    struct file *file;
    if ((ret = fd2file(fd, &file)) != 0) {
        return ret;
    }
    filemap_close(file);
    return 0;
}

int
file_read(int fd, void *base, size_t len, size_t *copied_store) {
    int ret;
    struct file *file;
    *copied_store = 0;
    if ((ret = fd2file(fd, &file)) != 0) {
        return ret;
    }
    if (!file->readable) {
        return -E_INVAL;
    }
    filemap_acquire(file);

    struct iobuf __iob, *iob = iobuf_init(&__iob, base, len, file->pos);
    ret = vop_read(file->node, iob);

    size_t copied = iobuf_used(iob);
    if (file->status == FD_OPENED) {
        file->pos += copied;
    }
    *copied_store = copied;
    filemap_release(file);
    return ret;
}

int
file_write(int fd, void *base, size_t len, size_t *copied_store) {
    int ret;
    struct file *file;
    *copied_store = 0;
    if ((ret = fd2file(fd, &file)) != 0) {
        return ret;
    }
    if (!file->writable) {
        return -E_INVAL;
    }
    filemap_acquire(file);

    struct iobuf __iob, *iob = iobuf_init(&__iob, base, len, file->pos);
    ret = vop_write(file->node, iob);

    size_t copied = iobuf_used(iob);
    if (file->status == FD_OPENED) {
        file->pos += copied;
    }
    *copied_store = copied;
    filemap_release(file);
    return ret;
}

int
file_seek(int fd, off_t pos, int whence) {
    struct stat __stat, *stat = &__stat;
    int ret;
    struct file *file;
    if ((ret = fd2file(fd, &file)) != 0) {
        return ret;
    }
    filemap_acquire(file);

    switch (whence) {
    case LSEEK_SET: break;
    case LSEEK_CUR: pos += file->pos; break;
    case LSEEK_END:
        if ((ret = vop_fstat(file->node, stat)) == 0) {
            pos += stat->st_size;
        }
        break;
    default: ret = -E_INVAL;
    }

    if (ret == 0) {
        if ((ret = vop_tryseek(file->node, pos)) == 0) {
            file->pos = pos;
        }
//	kprintf("file_seek, pos=%d, whence=%d, ret=%d\n", pos, whence, ret);
    }
    filemap_release(file);
    return ret;
}

int
file_fstat(int fd, struct stat *stat) {
    int ret;
    struct file *file;
    if ((ret = fd2file(fd, &file)) != 0) {
        return ret;
    }
    filemap_acquire(file);
    ret = vop_fstat(file->node, stat);
    filemap_release(file);
    return ret;
}

int
file_fsync(int fd) {
    int ret;
    struct file *file;
    if ((ret = fd2file(fd, &file)) != 0) {
        return ret;
    }
    filemap_acquire(file);
    ret = vop_fsync(file->node);
    filemap_release(file);
    return ret;
}

int
file_getdirentry(int fd, struct dirent *direntp) {
    int ret;
    struct file *file;
    if ((ret = fd2file(fd, &file)) != 0) {
        return ret;
    }
    filemap_acquire(file);

    struct iobuf __iob, *iob = iobuf_init(&__iob, direntp->name, sizeof(direntp->name), direntp->offset);
    if ((ret = vop_getdirentry(file->node, iob)) == 0) {
        direntp->offset += iobuf_used(iob);
    }
    filemap_release(file);
    return ret;
}

int
file_dup(int fd1, int fd2) {
    int ret;
    struct file *file1, *file2;
    if ((ret = fd2file(fd1, &file1)) != 0) {
        return ret;
    }
    if ((ret = filemap_alloc(fd2, &file2)) != 0) {
        return ret;
    }
    filemap_dup(file2, file1);
    return file2->fd;
}


int pipealloc(struct file *f0, struct file *f1)
{
    // 这里没有用预分配，由于 pipe 比较大，直接拿一个页过来，也不算太浪费
    struct pipe *pi = (struct pipe*)alloc_page();
    // 一开始 pipe 可读可写，但是已读和已写内容为 0
    pi->readopen = 1;
    pi->writeopen = 1;
    pi->nwrite = 0;
    pi->nread = 0;

    // 两个参数分别通过 filealloc 得到，把该 pipe 和这两个文件关连，一端可读，一端可写。读写端控制是 sys_pipe 的要求。
    f0->status = FD_PIPE;
    f0->readable = 1;
    f0->writable = 0;
    f0->pipe = pi;

    f1->status = FD_PIPE;
    f1->readable = 0;
    f1->writable = 1;
    f1->pipe = pi;
    return 0;
}

void pipeclose(struct pipe *pi, int writable)
{
    if(writable){
        pi->writeopen = 0;
    } else {
        pi->readopen = 0;
    }
    if(pi->readopen == 0 && pi->writeopen == 0){
        kfree((char*)pi);
    }
}

int pipewrite(struct pipe *pi, uint64_t addr, int n)
{
    // w 记录已经写的字节数
    int w = 0;
    while(w < n){
        // 若不可读，写也没有意义
        if(pi->readopen == 0){
            return -1;
        }
        struct mm_struct *p_mm=current->mm;  
        if(pi->nwrite == pi->nread + PIPESIZE){
            // pipe write 端已满，阻塞
            do_yield();
        } else {
            // 一次读的 size 为 min(用户buffer剩余，pipe 剩余写容量，pipe 剩余线性容量)
            uint64_t size =MIN(MIN(
                n - w,
                pi->nread + PIPESIZE - pi->nwrite),
                PIPESIZE - (pi->nwrite % PIPESIZE));
            // 使用 copyin 读入用户 buffer 内容
            copy_from_user(p_mm->pgdir, addr + w,&pi->data[pi->nwrite % PIPESIZE], size,1);
            pi->nwrite += size;
            w += size;
        }
    }
    return w;
}

int piperead(struct pipe *pi, uint64_t addr, int n)
{
    // r 记录已经写的字节数
    int r = 0;
    // 若 pipe 可读内容为空，阻塞或者报错
    while(pi->nread == pi->nwrite) {
        if(pi->writeopen)
            do_yield();
        else
            return -1;
    }
    struct mm_struct *p_mm=current->mm;  
    uint64_t size = MIN(MIN(n - r,pi->nwrite - pi->nread),
            PIPESIZE - (pi->nread % PIPESIZE));
    while(r < n && size != 0) {
        // pipe 可读内容为空，返回
        
        if(pi->nread == pi->nwrite)
            break;
        // 一次写的 size 为：min(用户buffer剩余，可读内容，pipe剩余线性容量)
        uint64_t size = MIN(
            MIN(n - r,pi->nwrite - pi->nread),
            PIPESIZE - (pi->nread % PIPESIZE)
        );
        // 使用 copyout 写用户内存
        copy_to_user(p_mm->pgdir,  &pi->data[pi->nread % PIPESIZE],addr + r, size);
        pi->nread += size;
        r += size;
    }
    return r;
}
