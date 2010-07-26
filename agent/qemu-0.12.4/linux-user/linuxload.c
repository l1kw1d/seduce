/* Code for loading Linux executables.  Mostly linux kernel code.  */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#include "qemu.h"

#define NGROUPS 32

/* ??? This should really be somewhere else.  */
abi_long memcpy_to_target(abi_ulong dest, const void *src,
                          unsigned long len)
{
    void *host_ptr;

    host_ptr = lock_user(VERIFY_WRITE, dest, len, 0);
    if (!host_ptr)
        return -TARGET_EFAULT;
    memcpy(host_ptr, src, len);
    unlock_user(host_ptr, dest, 1);
    return 0;
}

static int in_group_p(gid_t g)
{
    /* return TRUE if we're in the specified group, FALSE otherwise */
    int		ngroup;
    int		i;
    gid_t	grouplist[NGROUPS];

    ngroup = getgroups(NGROUPS, grouplist);
    for(i = 0; i < ngroup; i++) {
	if(grouplist[i] == g) {
	    return 1;
	}
    }
    return 0;
}

static int count(char ** vec)
{
    int		i;

    for(i = 0; *vec; i++) {
        vec++;
    }

    return(i);
}

static int prepare_binprm(struct linux_binprm *bprm)
{
    struct stat		st;
    int mode;
    int retval, id_change;

    if(fstat(bprm->fd, &st) < 0) {
	return(-errno);
    }

    mode = st.st_mode;
    if(!S_ISREG(mode)) {	/* Must be regular file */
	return(-EACCES);
    }
    if(!(mode & 0111)) {	/* Must have at least one execute bit set */
	return(-EACCES);
    }

    bprm->e_uid = geteuid();
    bprm->e_gid = getegid();
    id_change = 0;

    /* Set-uid? */
    if(mode & S_ISUID) {
    	bprm->e_uid = st.st_uid;
	if(bprm->e_uid != geteuid()) {
	    id_change = 1;
	}
    }

    /* Set-gid? */
    /*
     * If setgid is set but no group execute bit then this
     * is a candidate for mandatory locking, not a setgid
     * executable.
     */
    if ((mode & (S_ISGID | S_IXGRP)) == (S_ISGID | S_IXGRP)) {
	bprm->e_gid = st.st_gid;
	if (!in_group_p(bprm->e_gid)) {
		id_change = 1;
	}
    }

    retval = lseek(bprm->fd, 0L, SEEK_SET);
    if(retval >= 0) {
        retval = read(bprm->fd, bprm->buf, 128);
    }
    if(retval < 0) {
	perror("prepare_binprm");
	exit(-1);
	/* return(-errno); */
    }
    else {
	return(retval);
    }
}

/* Construct the envp and argv tables on the target stack.  */
abi_ulong loader_build_argptr(int envc, int argc, abi_ulong sp,
                              abi_ulong stringp, int push_ptr)
{
    TaskState *ts = (TaskState *)thread_env->opaque;
    int n = sizeof(abi_ulong);
    abi_ulong envp;
    abi_ulong argv;

    sp -= (envc + 1) * n;
    envp = sp;
    sp -= (argc + 1) * n;
    argv = sp;
    if (push_ptr) {
        /* FIXME - handle put_user() failures */
        sp -= n;
        put_user_ual(envp, sp);
        sp -= n;
        put_user_ual(argv, sp);
    }
    sp -= n;
    /* FIXME - handle put_user() failures */
    put_user_ual(argc, sp);
    ts->info->arg_start = stringp;
    while (argc-- > 0) {
        /* FIXME - handle put_user() failures */
        put_user_ual(stringp, argv);
        argv += n;
        stringp += target_strlen(stringp) + 1;
    }
    ts->info->arg_end = stringp;
    /* FIXME - handle put_user() failures */
    put_user_ual(0, argv);
    while (envc-- > 0) {
        /* FIXME - handle put_user() failures */
        put_user_ual(stringp, envp);
        envp += n;
        stringp += target_strlen(stringp) + 1;
    }
    /* FIXME - handle put_user() failures */
    put_user_ual(0, envp);

    return sp;
}

int loader_exec(void *data, size_t len, struct target_pt_regs * regs, 
		struct image_info *infop, unsigned long stack_base)
{
    infop->host_argv = NULL;
    load_raw_binary(data, len, infop, stack_base);
    do_init_thread(regs, infop);
    return 0;
}

