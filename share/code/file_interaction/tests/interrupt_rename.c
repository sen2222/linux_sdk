#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <signal.h>

/* Inject cancellation exactly when the actual commit begins. */
int rename(const char *old_path, const char *new_path)
{
    int (*real_rename)(const char *, const char *) = dlsym(RTLD_NEXT, "rename");
    if(!real_rename) { errno = ENOSYS; return -1; }
    raise(SIGTERM);
    return real_rename(old_path, new_path);
}
