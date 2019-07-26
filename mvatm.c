#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <error.h>

#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define mvatm_error(errno) error_at_line(\
    1, (errno), __FILE__, __LINE__, "%s", __FUNCTION__);

// try get the correct blocksize
// but fail back to a default if that fails
// TODO couldn't this be bigger, save syscalls?
static size_t get_buf_size(int fd)
{
    struct stat statbuf;
    if (fstat(fd, &statbuf))
    {
        fprintf(stderr, "stat failed\n");
        return BUFSIZ;
    }
    return statbuf.st_blksize;
}

static int mvatm_exdev(const char* oldpath, const char* newpath)
{
    // not freeing or closing on failures, since we exit anyway
    char * newpath_tmp;
    if (asprintf(&newpath_tmp, "%s.part", newpath) < 0)
    {
        mvatm_error(errno);
        fprintf(stderr, "Memory allocation error\n");
        return 1;
    }

    // O_TMPFILE is also interesting, but probably not supported on fs
    // manpage contains a workaround for NFS, TODO look at that

    // open a tmp file to write the data into
    // fail if that exists already
    int fd_new;
    if ((fd_new = open(newpath_tmp, O_WRONLY|O_CREAT|O_EXCL, S_IRUSR|S_IWUSR)) < 0)
    {
        mvatm_error(errno);
        return 1;
    }

    int fd_old;
    if ((fd_old = open(oldpath, O_RDONLY)) < 0)
    {
        mvatm_error(errno);
        return 1;
    }

    size_t blksize = get_buf_size(fd_new);
    char * buf = malloc(blksize);
    if (!buf)
    {
        fprintf(stderr, "Memory allocation error\n");
        return 1;
    }

    // cat one file to the other
    ssize_t bytes_read;
    while ((bytes_read = read(fd_old, buf, blksize)) > 0)
    {
        char *wbuf = buf;
        ssize_t bytes_left = bytes_read;
        ssize_t bytes_written;
        while (bytes_left && (bytes_written = write(
                fd_new,
                wbuf,
                bytes_left)
            ) != -1)
        {
            wbuf += bytes_written;
            bytes_left -= bytes_written;
        }
        if (bytes_written < 0)
        {
            fprintf(stderr, "Write error\n");
            return 1;
        }
    }
    if (bytes_read != 0)
    {
        fprintf(stderr, "Did not read whole file\n");
        return 1;
    }

    if (link(newpath_tmp, newpath))
    {
        mvatm_error(errno);
    }

    if (unlink(newpath_tmp))
    {
        mvatm_error(errno);
    }

    return 0;
}

int main(int argc, char const *argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "Provide two filenames\n");
        return 1;
    }

    const char* oldpath = argv[1];
    const char* newpath = argv[2];

    if (link(oldpath, newpath))
    {
        int link_error = errno;
        // fprintf(stderr, "%d %s\n", errno, strerror(errno));
        switch (link_error)
        {
            case EXDEV:
                return mvatm_exdev(oldpath, newpath);
            default:
                mvatm_error(link_error);
        }
    }
}
