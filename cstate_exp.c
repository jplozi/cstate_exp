#include <asm/mwait.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/kallsyms.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/tick.h>
#include <linux/tty.h>

#define MWAIT_DURATION_MS 10000
#define MWAIT_CPU 4
#define MWAIT_WAKER 3 
#define MWAIT_ARG2 0

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jean-Pierre Lozi");
MODULE_DESCRIPTION("A kernel module to test setting a C-state with "
                   "MONITOR/MWAIT.");
MODULE_VERSION("0.01");

#define KPROBE_PRE_HANDLER(fname) \
    static int __kprobes fname(struct kprobe *p, struct pt_regs *regs)

typedef unsigned long (*kallsyms_lookup_name_t)(const char *name);
typedef void (*tick_nohz_idle_stop_tick_t) (void);
typedef void (*tick_nohz_idle_restart_tick_t) (void);
typedef unsigned int (*aperfmperf_get_khz_t) (int);
typedef struct task_struct *(*kthread_create_on_cpu_t) (int (*)(void *),
                                                        void *,
                                                        unsigned int,
                                                        const char *);

static kallsyms_lookup_name_t __kallsyms_lookup_name;
static tick_nohz_idle_stop_tick_t __tick_nohz_idle_stop_tick;
static tick_nohz_idle_restart_tick_t __tick_nohz_idle_restart_tick;
static aperfmperf_get_khz_t __aperfmperf_get_khz;
static kthread_create_on_cpu_t __kthread_create_on_cpu;

static struct kprobe kp0, kp1;
struct task_struct *task1, *task2;
unsigned int shared_var __attribute__((aligned(64))) = 0;

KPROBE_PRE_HANDLER(handler_pre0)
{
    __kallsyms_lookup_name = (kallsyms_lookup_name_t) (--regs->ip); 
    return 0;
}

KPROBE_PRE_HANDLER(handler_pre1)
{
    return 0;
}

static int do_register_kprobe(struct kprobe *kp, char *symbol_name, void *handler)
{
    int ret;

    kp->symbol_name = symbol_name;
    kp->pre_handler = handler;

    ret = register_kprobe(kp);
    if (ret < 0) {
        pr_err("register_probe() for symbol %s failed, returned %d\n",
               symbol_name, ret);
        return ret;
    }

    pr_info("Planted kprobe for symbol %s at %p\n", symbol_name, kp->addr);

    return ret;
}

static void print_freq(char *str)
{
    unsigned int freq = __aperfmperf_get_khz(MWAIT_CPU);
    printk("[cstate_exp] %s frequency: %u.%03u\n", str, freq / 1000,
           freq % 1000);
}

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

int thread_waker(void *data)
{
    msleep(MWAIT_DURATION_MS);
    shared_var = 1;

    while(!kthread_should_stop()){
        schedule();
    }
    return 0;
}

static int thread_mwait(void *data)
{
    volatile int i;

    mb();
    clflush(&shared_var); /* Necessary? */

    do {
        mb();
        local_irq_disable();
        printk("[cstate_exp] MONITOR\n");
        __monitor(&shared_var, 0, 0);
        local_irq_enable();

        if (shared_var != 0)
        {
            printk("[cstate_exp] shared_var != 0, it should be 0! "
                   "Continuing anyway...\n");
        }

        print_freq("Pre-spinloop");
        printk("[cstate_exp] SPINLOOP\n");
        for (i = 0; i < 100000000; i++)
            ;
        printk("Post-spinloop");
        print_freq("Post-spinloop");

        mb();
        local_irq_disable();
        __tick_nohz_idle_stop_tick();
        print_freq("Pre-MWAIT");
        printk("[cstate_exp] MWAIT\n");
        __mwait(0, MWAIT_ARG2);
        printk("[cstate_exp] WOKEN UP\n");
        print_freq("Post-MWAIT");
        __tick_nohz_idle_restart_tick();
        local_irq_enable();

        if (shared_var != 1)
        {
            printk("[cstate_exp] shared_var != 1, it should be 1! "
                   "Continuing anyway...\n");
        }
    } while (shared_var != 1);

    while(!kthread_should_stop()){
        schedule();
    }

    return 0;
}

static int __init cstate_exp_init(void)
{
    int ret;

    write_console("[cstate_exp] Init\r\n");

    if (cpu_has(&boot_cpu_data, X86_FEATURE_MWAIT))
    {
        write_console("[cstate_exp] MONITOR/MWAIT is supported!\r\n");
    } 
    else
    {
        write_console("[cstate_exp] MONITOR/MWAIT is *NOT* supported! "
                      "Continuing anyway...\r\n");
    }

    ret = do_register_kprobe(&kp0, "kallsyms_lookup_name", handler_pre0);
    if (ret < 0)
      return ret;
 
    ret = do_register_kprobe(&kp1, "kallsyms_lookup_name", handler_pre1);
    if (ret < 0) {
      unregister_kprobe(&kp0);
      return ret;
    }
  
    unregister_kprobe(&kp0);
    unregister_kprobe(&kp1);
  
    __tick_nohz_idle_stop_tick =
        (tick_nohz_idle_stop_tick_t)
            __kallsyms_lookup_name("tick_nohz_idle_stop_tick");
    __tick_nohz_idle_restart_tick =
        (tick_nohz_idle_restart_tick_t)
            __kallsyms_lookup_name("tick_nohz_idle_restart_tick");
    __aperfmperf_get_khz =
        (aperfmperf_get_khz_t)
            __kallsyms_lookup_name("aperfmperf_get_khz");
    __kthread_create_on_cpu =
        (kthread_create_on_cpu_t)
            __kallsyms_lookup_name("kthread_create_on_cpu");

    task1 = __kthread_create_on_cpu(&thread_waker, NULL, MWAIT_WAKER,
                                    "thread_waker");
    task2 = __kthread_create_on_cpu(&thread_mwait, NULL, MWAIT_CPU,
                                    "thread_mwait");

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

