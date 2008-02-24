#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#include "qemu.h"
#include "exec-all.h"
#include "debug.h"
#include "detect_engine.h"

extern unsigned long x86_stack_size;

/* Globals */
char *threat_payload;
size_t threat_length;
sigjmp_buf env;

void sigvtalrm_handler(int signum)
{
    tb_lock = SPIN_LOCK_UNLOCKED;
    siglongjmp(env, 100);
}

void cleanup(void)
{
    int c;
    for (c = 1; c < 24; c++)
    {
        free(struct_entries[c].field_offsets[0]);
        free(struct_entries[c].field_offsets[1]);
    }
    memset(struct_entries, 0, sizeof(StructEntry) * 128);
}

char *getBlock(char *data, size_t len, int min, int reset)
{
    data[len - 1] = 0;

    static char *p = NULL;
    if (reset) p = data;
    char *last;
    
    for (last = p; p < data + len; p++)
    {
        if (!*p)
        {
            if (p - last < min)
                last = p + 1;
            else
            {
                p++;
                return last;
            }
        }
    }
    return NULL;
}

int execute_work(char *data, size_t len, QemuVars *qv)
{
    char *p;
    void *block;
    int reset = 1, blocksize, l = 0, i, ret;
    char tmp[26];
    memset(tmp, 0, 26);

    if((data == NULL) || (len == 0))
        return WORK_DONE;
 
    while ((p = getBlock(data, len, 30, reset)) != NULL)
    {
        reset = 0;
        l++;

        blocksize = strlen(p);
        block = calloc(1, blocksize + 1);
        if (block == NULL) {
            fprintf(stderr,"calloc failed\n");
            exit(1);
        }

        for (i = 0; i < blocksize - 5; i++) {
            memcpy(block, p, blocksize);
            DPRINTF("block %d - byte %.2d - ", l, i);
            if (sigsetjmp(env,1) == 100) {
                DPRINTF("Endless Loop detected!\n");
                setitimer(ITIMER_VIRTUAL, &qv->zvalue, (struct itimerval*) NULL);
                goto prepare_next_iter;
            }
    	    setitimer(ITIMER_VIRTUAL, &qv->value, (struct itimerval*) NULL);
            ret = qemu_exec(block + i, blocksize - i, qv->stack_base, qv->cpu);
            setitimer(ITIMER_VIRTUAL, &qv->zvalue, (struct itimerval*) NULL);

            switch(ret) {
                case HIGH_RISK_SYSCALL:
                    DPRINTF("High risk syscall - %d\n", qv->cpu->regs[R_EAX]);
                    snprintf(tmp, 25, "syscall - %d", qv->cpu->regs[R_EAX]);
                    threat_length  = strlen(tmp);
                    threat_payload = strndup(tmp, threat_length);
                    cleanup();
                    free(block);
                    return THREAT_DETECTED;
                case EXIT_SYSCALL:
                    DPRINTF("Syscall exit\n");
                    break;
                case EXCEPTION_INTERRUPT:
                    DPRINTF("exception - INTERRUPT\n");
                    break;
                case EXCEPTION_NOSEG:
                    DPRINTF("exception - NOSEG\n");
                    break;
                case EXCEPTION_STACK:
                    DPRINTF("exception - Stack Fault\n");
                    break;
                case EXCEPTION_GPF:
                    DPRINTF("exception - General Protection Fault\n");
                    break;
                case EXCEPTION_PAGE:
                    DPRINTF("exception - Page Fault\n");
                    break;
                case EXCEPTION_DIVZ:
                    DPRINTF("exception - Division by Zero\n");
                    break;
                case EXCEPTION_SSTP:
                    DPRINTF("exception - SSTP\n");
                    break;
                case EXCEPTION_INT3:
                    DPRINTF("exception - INT3\n");
                    break;
                case EXCEPTION_INTO:
                    DPRINTF("exception - INTO\n");
                    break;
                case EXCEPTION_BOUND:
                    DPRINTF("exception - BOUND\n");
                    break;
                case EXCEPTION_ILLOP:
                    DPRINTF("exception - Illegal Operation\n");
                    break;
                case EXCEPTION_DEBUG:
                    DPRINTF("exception - DEBUG\n");
                    break;
                default:
                    DPRINTF("unknown exception\n");
            }
prepare_next_iter:
            cleanup();
        }
        free(block);
    }
    return NEED_NEXT;
}

void detect_engine_init(QemuVars *qv)
{
    qv->value.it_interval.tv_usec =
    qv->value.it_value.tv_usec = 1000;
    qv->value.it_interval.tv_sec =
    qv->value.it_value.tv_sec = 0;

    qv->zvalue.it_interval.tv_sec =
    qv->zvalue.it_interval.tv_usec =
    qv->zvalue.it_value.tv_sec =
    qv->zvalue.it_value.tv_usec = 0;

    qv->cpu = malloc(sizeof(CPUX86State));
    qv->stack_base = setup_stack();
}

void detect_engine_stop(QemuVars *qv)
{
    free(qv->cpu);

    if (munmap((void *)qv->stack_base - x86_stack_size, x86_stack_size) == -1) {
        perror("munmap stack_base");
        exit(1);
    }
}
