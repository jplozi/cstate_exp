#include <asm/mwait.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/kallsyms.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/tick.h>
#include <linux/tty.h>

#define MWAIT_DURATION_MS 5000

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jean-Pierre Lozi");
MODULE_DESCRIPTION("A kernel module to test setting a C-state with "
                   "MONITOR/MWAIT.");
MODULE_VERSION("0.01");

typedef void (*tick_nohz_idle_stop_tick_t) (void);
typedef void (*tick_nohz_idle_restart_tick_t) (void);
typedef struct task_struct *(*kthread_create_on_cpu_t) (int (*)(void *),
                                                        void *,
                                                        unsigned int,
                                                        const char *);

struct task_struct *task1, *task2;
unsigned int shared_var __attribute__((aligned(64))) = 0;

int write_console(char *str)
{
    struct tty_struct *my_tty;
    if((my_tty=current->signal->tty) != NULL)
    {
        ((my_tty->driver->ops->write) (my_tty,str,strlen(str)));
        return 0;
    }
    else return -1;
}

int thread_func1(void *data)
{
    msleep(MWAIT_DURATION_MS);
    shared_var = 1;

    while(!kthread_should_stop()){
        schedule();
    }
    return 0;
}

int thread_func2(void *data)
{
    tick_nohz_idle_stop_tick_t tick_nohz_idle_stop_tick =
        (tick_nohz_idle_stop_tick_t)
            kallsyms_lookup_name("tick_nohz_idle_stop_tick");
    tick_nohz_idle_restart_tick_t tick_nohz_idle_restart_tick =
        (tick_nohz_idle_restart_tick_t)
            kallsyms_lookup_name("tick_nohz_idle_restart_tick");

    mb();
    clflush(&shared_var); /* Necessary? */

    do {
        printk("[cstate_exp] MONITOR\n");

        mb();
        local_irq_disable();
        __monitor(&shared_var, 0, 0);
        local_irq_enable();

        if (shared_var != 0)
        {
            printk("[cstate_exp] shared_var != 0, it should be 0! "
                   "Continuing anyway...\n");
        }

        printk("[cstate_exp] MWAIT\n");

        mb();
        local_irq_disable();
        tick_nohz_idle_stop_tick();
        __mwait(0, 0);
        tick_nohz_idle_restart_tick();
        local_irq_enable();

        if (shared_var != 1)
        {
            printk("[cstate_exp] shared_var != 1, it should be 1! "
                   "Continuing anyway...\n");
        }

        printk("[cstate_exp] WOKEN UP\n");

    } while (shared_var != 1);

    while(!kthread_should_stop()){
        schedule();
    }

    return 0;
}

static int __init cstate_exp_init(void)
{
    struct cpuinfo_x86 *c = &boot_cpu_data;
    kthread_create_on_cpu_t kthread_create_on_cpu =
        (kthread_create_on_cpu_t) kallsyms_lookup_name("kthread_create_on_cpu");

    write_console("[cstate_expr] Init\r\n");

    if (cpu_has(c, X86_FEATURE_MWAIT))
    {
        write_console("[cstate_expr] MONITOR/MWAIT is supported!\r\n");
    } 
    else
    {
        write_console("[cstate_expr] MONITOR/MWAIT is *NOT* supported! "
                      "Continuing anyway...\r\n");
    }

    task1 = kthread_create_on_cpu(&thread_func1, NULL, 1, "thread_func1");
    task2 = kthread_create_on_cpu(&thread_func2, NULL, 2, "thread_func2");

    wake_up_process(task1);
    wake_up_process(task2);

    return 0;
}

static void __exit cstate_exp_exit(void)
{
    /* Make sure we don't remove the module while task2 is in its MWAIT. */
    shared_var = 1;
    msleep(1000);

    kthread_stop(task1);
    kthread_stop(task2);

    write_console("[cstate_exp] Exit\r\n");
}

module_init(cstate_exp_init);
module_exit(cstate_exp_exit);

