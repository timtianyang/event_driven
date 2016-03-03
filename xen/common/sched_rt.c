/*****************************************************************************
 * Preemptive Global Earliest Deadline First  (EDF) scheduler for Xen
 * EDF scheduling is a real-time scheduling algorithm used in embedded field.
 *
 * by Sisu Xi, 2013, Washington University in Saint Louis
 * and Meng Xu, 2014, University of Pennsylvania
 *
 * based on the code of credit Scheduler
 */

#include <xen/config.h>
#include <xen/init.h>
#include <xen/lib.h>
#include <xen/sched.h>
#include <xen/domain.h>
#include <xen/delay.h>
#include <xen/event.h>
#include <xen/time.h>
#include <xen/timer.h>
#include <xen/perfc.h>
#include <xen/sched-if.h>
#include <xen/softirq.h>
#include <asm/atomic.h>
#include <xen/errno.h>
#include <xen/trace.h>
#include <xen/cpu.h>
#include <xen/keyhandler.h>
#include <xen/trace.h>
#include <xen/guest_access.h>

/*
 * TODO:
 *
 * Migration compensation and resist like credit2 to better use cache;
 * Lock Holder Problem, using yield?
 * Self switch problem: VCPUs of the same domain may preempt each other;
 */

/*
 * Design:
 *
 * This scheduler follows the Preemptive Global Earliest Deadline First (EDF)
 * theory in real-time field.
 * At any scheduling point, the VCPU with earlier deadline has higher priority.
 * The scheduler always picks highest priority VCPU to run on a feasible PCPU.
 * A PCPU is feasible if the VCPU can run on this PCPU and (the PCPU is idle or
 * has a lower-priority VCPU running on it.)
 *
 * Each VCPU has a dedicated period and budget.
 * The deadline of a VCPU is at the end of each period;
 * A VCPU has its budget replenished at the beginning of each period;
 * While scheduled, a VCPU burns its budget.
 * The VCPU needs to finish its budget before its deadline in each period;
 * The VCPU discards its unused budget at the end of each period.
 * If a VCPU runs out of budget in a period, it has to wait until next period.
 *
 * Each VCPU is implemented as a deferable server.
 * When a VCPU has a task running on it, its budget is continuously burned;
 * When a VCPU has no task but with budget left, its budget is preserved.
 *
 * Queue scheme:
 * A global runqueue and a global depletedqueue for each CPU pool.
 * The runqueue holds all runnable VCPUs with budget, sorted by deadline;
 * The depletedqueue holds all VCPUs without budget, unsorted;
 *
 * Note: cpumask and cpupool is supported.
 */

/*
 * Locking:
 * A global system lock is used to protect the RunQ and DepletedQ.
 * The global lock is referenced by schedule_data.schedule_lock
 * from all physical cpus.
 *
 * The lock is already grabbed when calling wake/sleep/schedule/ functions
 * in schedule.c
 *
 * The functions involes RunQ and needs to grab locks are:
 *    vcpu_insert, vcpu_remove, context_saved, __runq_insert
 */


/*
 * Default parameters:
 * Period and budget in default is 10 and 4 ms, respectively
 */
#define RTDS_DEFAULT_PERIOD     (MICROSECS(10000))
#define RTDS_DEFAULT_BUDGET     (MICROSECS(4000))

#define UPDATE_LIMIT_SHIFT      10

/*
 * Flags
 */
/*
 * RTDS_scheduled: Is this vcpu either running on, or context-switching off,
 * a phyiscal cpu?
 * + Accessed only with global lock held.
 * + Set when chosen as next in rt_schedule().
 * + Cleared after context switch has been saved in rt_context_saved()
 * + Checked in vcpu_wake to see if we can add to the Runqueue, or if we should
 *   set RTDS_delayed_runq_add
 * + Checked to be false in runq_insert.
 */
#define __RTDS_scheduled            1
#define RTDS_scheduled (1<<__RTDS_scheduled)
/*
 * RTDS_delayed_runq_add: Do we need to add this to the RunQ/DepletedQ
 * once it's done being context switching out?
 * + Set when scheduling out in rt_schedule() if prev is runable
 * + Set in rt_vcpu_wake if it finds RTDS_scheduled set
 * + Read in rt_context_saved(). If set, it adds prev to the Runqueue/DepletedQ
 *   and clears the bit.
 */
#define __RTDS_delayed_runq_add     2
#define RTDS_delayed_runq_add (1<<__RTDS_delayed_runq_add)

/*
 * rt tracing events ("only" 512 available!). Check
 * include/public/trace.h for more details.
 */
#define TRC_RTDS_TICKLE           TRC_SCHED_CLASS_EVT(RTDS, 1)
#define TRC_RTDS_RUNQ_PICK        TRC_SCHED_CLASS_EVT(RTDS, 2)
#define TRC_RTDS_BUDGET_BURN      TRC_SCHED_CLASS_EVT(RTDS, 3)
#define TRC_RTDS_BUDGET_REPLENISH TRC_SCHED_CLASS_EVT(RTDS, 4)
#define TRC_RTDS_SCHED_TASKLET    TRC_SCHED_CLASS_EVT(RTDS, 5)

 /*
  * Useful to avoid too many cpumask_var_t on the stack.
  */
static cpumask_var_t *_cpumask_scratch;
#define cpumask_scratch _cpumask_scratch[smp_processor_id()]

/*
 * We want to only allocate the _cpumask_scratch array the first time an
 * instance of this scheduler is used, and avoid reallocating and leaking
 * the old one when more instance are activated inside new cpupools. We
 * also want to get rid of it when the last instance is de-inited.
 *
 * So we (sort of) reference count the number of initialized instances. This
 * does not need to happen via atomic_t refcounters, as it only happens either
 * during boot, or under the protection of the cpupool_lock spinlock.
 */
static unsigned int nr_rt_ops;

/* handler for the replenishment timer */
static void repl_handler(void *data);

/*
 * Systme-wide private data, include global RunQueue/DepletedQ
 * Global lock is referenced by schedule_data.schedule_lock from all
 * physical cpus. It can be grabbed via vcpu_schedule_lock_irq()
 */
struct rt_private {
    spinlock_t lock;            /* the global coarse grand lock */
    struct list_head sdom;      /* list of availalbe domains, used for dump */
    struct list_head runq;      /* ordered list of runnable vcpus */
    struct list_head depletedq; /* unordered list of depleted vcpus */
    struct list_head replq;     /* ordered list of vcpus that need replenishment */
    cpumask_t tickled;          /* cpus been tickled */
    struct timer *repl_timer;   /* replenishment timer */
};

/*
 * Virtual CPU
 */
struct rt_vcpu {
    struct list_head q_elem;    /* on the runq/depletedq list */
    struct list_head replq_elem;/* on the repl event list */

    /* Up-pointers */
    struct rt_dom *sdom;
    struct vcpu *vcpu;

    /* VCPU parameters, in nanoseconds */
    s_time_t period;
    s_time_t budget;

    /* VCPU current infomation in nanosecond */
    s_time_t cur_budget;        /* current budget */
    s_time_t last_start;        /* last start time */
    s_time_t cur_deadline;      /* current deadline for EDF */

    unsigned flags;             /* mark __RTDS_scheduled, etc.. */
    /* mode change stuff */
    unsigned active;            /* if active in mode change */
    unsigned type;              /* old only, new only, unchanged, changed */
    unsigned mode;              /* for changed vcpu only. 0:old mode, 1:new mode */
    struct xen_domctl_sched_rtds new_param;
    struct list_head type_elem; /* on the type list */
};

/*
 * Domain
 */
struct rt_dom {
    struct list_head sdom_elem; /* link list on rt_priv */
    struct domain *dom;         /* pointer to upper domain */
};

/* mode change related control vars */
struct mode_change{
    mode_change_info_t info;  /* mc protocol */
    unsigned in_trans;          /* if rtds is in transition */
    s_time_t recv;              /* when MCR was received */
    struct list_head old_vcpus;
    struct list_head new_vcpus;
    struct list_head unchanged_vcpus;
    struct list_head changed_vcpus;

} rtds_mc;

/*
 * Useful inline functions
 */
static inline struct rt_private *rt_priv(const struct scheduler *ops)
{
    return ops->sched_data;
}

static inline struct rt_vcpu *rt_vcpu(const struct vcpu *vcpu)
{
    return vcpu->sched_priv;
}

static inline struct rt_dom *rt_dom(const struct domain *dom)
{
    return dom->sched_priv;
}

static inline struct list_head *rt_runq(const struct scheduler *ops)
{
    return &rt_priv(ops)->runq;
}

static inline struct list_head *rt_depletedq(const struct scheduler *ops)
{
    return &rt_priv(ops)->depletedq;
}

static inline struct list_head *rt_replq(const struct scheduler *ops)
{
    return &rt_priv(ops)->replq;
}

static struct rt_vcpu*
__type_elem(struct list_head *elem)
{
    return list_entry(elem, struct rt_vcpu, type_elem);
}

/*
 * Helper functions for manipulating the runqueue, the depleted queue,
 * and the replenishment events queue.
 */
static int
__vcpu_on_q(const struct rt_vcpu *svc)
{
   return !list_empty(&svc->q_elem);
}

static struct rt_vcpu *
__q_elem(struct list_head *elem)
{
    return list_entry(elem, struct rt_vcpu, q_elem);
}

static struct rt_vcpu *
replq_elem(struct list_head *elem)
{
    return list_entry(elem, struct rt_vcpu, replq_elem);
}

static int
vcpu_on_replq(const struct rt_vcpu *svc)
{
   return !list_empty(&svc->replq_elem);
}

/*
 * Debug related code, dump vcpu/cpu information
 */
static void
rt_dump_vcpu(const struct scheduler *ops, const struct rt_vcpu *svc)
{
    cpumask_t *cpupool_mask, *mask;

    ASSERT(svc != NULL);
    /* idle vcpu */
    if( svc->sdom == NULL )
    {
        printk("\n");
        return;
    }

    /*
     * We can't just use 'cpumask_scratch' because the dumping can
     * happen from a pCPU outside of this scheduler's cpupool, and
     * hence it's not right to use the pCPU's scratch mask (which
     * may even not exist!). On the other hand, it is safe to use
     * svc->vcpu->processor's own scratch space, since we hold the
     * runqueue lock.
     */
    mask = _cpumask_scratch[svc->vcpu->processor];

    cpupool_mask = cpupool_domain_cpumask(svc->vcpu->domain);
    cpumask_and(mask, cpupool_mask, svc->vcpu->cpu_hard_affinity);
    cpulist_scnprintf(keyhandler_scratch, sizeof(keyhandler_scratch), mask);
    printk("[%5d.%-2u] cpu %u, (%"PRI_stime", %"PRI_stime"),"
           " cur_b=%"PRI_stime" cur_d=%"PRI_stime" last_start=%"PRI_stime"\n"
           " \t\t onQ=%d runnable=%d active=%d type=%d flags=%x effective hard_affinity=%s\n",
            svc->vcpu->domain->domain_id,
            svc->vcpu->vcpu_id,
            svc->vcpu->processor,
            svc->period,
            svc->budget,
            svc->cur_budget,
            svc->cur_deadline,
            svc->last_start,
            __vcpu_on_q(svc),
            vcpu_runnable(svc->vcpu),
            svc->active,
            svc->type,
            svc->flags,
            keyhandler_scratch);
}

static void
rt_dump_pcpu(const struct scheduler *ops, int cpu)
{
    struct rt_private *prv = rt_priv(ops);
    unsigned long flags;

    spin_lock_irqsave(&prv->lock, flags);
    rt_dump_vcpu(ops, rt_vcpu(curr_on_cpu(cpu)));
    spin_unlock_irqrestore(&prv->lock, flags);
}

static void
rt_dump(const struct scheduler *ops)
{
    struct list_head *runq, *depletedq, *replq, *iter;
    struct rt_private *prv = rt_priv(ops);
    struct rt_vcpu *svc;
    struct rt_dom *sdom;
    unsigned long flags;

    spin_lock_irqsave(&prv->lock, flags);

    if ( list_empty(&prv->sdom) )
        goto out;

    runq = rt_runq(ops);
    depletedq = rt_depletedq(ops);
    replq = rt_replq(ops);

    printk("Global RunQueue info:\n");
    list_for_each( iter, runq )
    {
        svc = __q_elem(iter);
        rt_dump_vcpu(ops, svc);
    }

    printk("Global DepletedQueue info:\n");
    list_for_each( iter, depletedq )
    {
        svc = __q_elem(iter);
        rt_dump_vcpu(ops, svc);
    }

    printk("Global Replenishment Event info:\n");
    list_for_each( iter, replq )
    {
        svc = replq_elem(iter);
        rt_dump_vcpu(ops, svc);
    }

    printk("Domain info:\n");
    list_for_each( iter, &prv->sdom )
    {
        struct vcpu *v;

        sdom = list_entry(iter, struct rt_dom, sdom_elem);
        printk("\tdomain: %d\n", sdom->dom->domain_id);

        for_each_vcpu ( sdom->dom, v )
        {
            svc = rt_vcpu(v);
            rt_dump_vcpu(ops, svc);
        }
    }

 out:
    spin_unlock_irqrestore(&prv->lock, flags);
}

/*
 * update deadline and budget when now >= cur_deadline
 * it need to be updated to the deadline of the current period
 */
static void
rt_update_deadline(s_time_t now, struct rt_vcpu *svc)
{
    ASSERT(now >= svc->cur_deadline);
    ASSERT(svc->period != 0);

    if ( svc->cur_deadline + (svc->period << UPDATE_LIMIT_SHIFT) > now )
    {
        do
            svc->cur_deadline += svc->period;
        while ( svc->cur_deadline <= now );
    }
    else
    {
        long count = ((now - svc->cur_deadline) / svc->period) + 1;
        svc->cur_deadline += count * svc->period;
    }

    svc->cur_budget = svc->budget;

    /* TRACE */
    {
        struct {
            unsigned dom:16,vcpu:16;
            unsigned cur_deadline_lo, cur_deadline_hi;
            unsigned cur_budget_lo, cur_budget_hi;
        } d;
        d.dom = svc->vcpu->domain->domain_id;
        d.vcpu = svc->vcpu->vcpu_id;
        d.cur_deadline_lo = (unsigned) svc->cur_deadline;
        d.cur_deadline_hi = (unsigned) (svc->cur_deadline >> 32);
        d.cur_budget_lo = (unsigned) svc->cur_budget;
        d.cur_budget_hi = (unsigned) (svc->cur_budget >> 32);
        trace_var(TRC_RTDS_BUDGET_REPLENISH, 1,
                  sizeof(d),
                  (unsigned char *) &d);
    }

    return;
}

/* 
 * A helper function that only removes a vcpu from a queue 
 * and it returns 1 if the vcpu was in front of the list.
 */
static inline int
deadline_queue_remove(struct list_head *queue, struct list_head *elem)
{
    int pos = 0;
    if( queue->next != elem )
        pos = 1;
    list_del_init(elem);
    return !pos;
}

static inline void
__q_remove(struct rt_vcpu *svc)
{
    if ( __vcpu_on_q(svc) )
        list_del_init(&svc->q_elem);
}

static inline void
__type_remove(struct rt_vcpu *svc)
{
    printk("remove vcpu%d from type\n", svc->vcpu->vcpu_id);
    list_del_init(&svc->type_elem);
}

/*
 * Removing a vcpu from the replenishment queue could
 * re-program the timer for the next replenishment event
 * if it was at the front of the list.
 */
static inline void
__replq_remove(const struct scheduler *ops, struct rt_vcpu *svc)
{
    struct rt_private *prv = rt_priv(ops);
    struct list_head *replq = rt_replq(ops);
    struct timer* repl_timer = prv->repl_timer;

    if( deadline_queue_remove(replq,&svc->replq_elem) )
    {
        stop_timer(repl_timer);
        /* re-arm the timer for the next replenishment event */
        if( !list_empty(replq) )
        {
            struct rt_vcpu *svc_next = replq_elem(replq->next);
            set_timer(repl_timer, svc_next->cur_deadline);
        }
    }
}

/*
 * An utility function that inserts a vcpu to a
 * queue based on certain order (EDF). The returned
 * value is 1 if a vcpu has been inserted to the
 * front of a list
 */
static int
deadline_queue_insert(struct rt_vcpu * (*_get_q_elem)(struct list_head *elem),
    struct rt_vcpu *svc, struct list_head *elem, struct list_head *queue)
{
    struct list_head *iter;
    int pos = 0;

    list_for_each(iter, queue)
    {
        struct rt_vcpu * iter_svc = (*_get_q_elem)(iter);
        if ( svc->cur_deadline <= iter_svc->cur_deadline )
            break;

        pos++;
    }

    list_add_tail(elem, iter);
    return !pos;
}

/*
 * Insert svc with budget in RunQ according to EDF:
 * vcpus with smaller deadlines go first.
 * Insert svc without budget in DepletedQ unsorted;
 */
static void
__runq_insert(const struct scheduler *ops, struct rt_vcpu *svc)
{
    struct rt_private *prv = rt_priv(ops);
    struct list_head *runq = rt_runq(ops);

    ASSERT( spin_is_locked(&prv->lock) );

    ASSERT( !__vcpu_on_q(svc) );

    ASSERT( vcpu_on_replq(svc) );
    /* add svc to runq if svc still has budget */
    if ( svc->cur_budget > 0 )
        deadline_queue_insert(&__q_elem, svc, &svc->q_elem, runq);
    else
        list_add(&svc->q_elem, &prv->depletedq);
}

/*
 * Insert svc into the replenishment event list
 * in replenishment time order.
 * vcpus that need to be replished earlier go first.
 * The timer may be re-programmed if svc is inserted
 * at the front of the event list.
 */
static void
__replq_insert(const struct scheduler *ops, struct rt_vcpu *svc)
{
    struct list_head *replq = rt_replq(ops);
    struct rt_private *prv = rt_priv(ops);
    struct timer *repl_timer = prv->repl_timer;

    ASSERT( !vcpu_on_replq(svc) );

    if( deadline_queue_insert(&replq_elem, svc, &svc->replq_elem, replq) )
        set_timer(repl_timer,svc->cur_deadline);
}

/* mode change helper functions */
static int
_runq_empty(const struct scheduler *ops)
{
    struct rt_private *prv = rt_priv(ops);
    return list_empty(&prv->runq);
}

static void
_update_changed_vcpu(struct rt_vcpu *svc)
{
    ASSERT( svc->type == CHANGED);

   // printk("updating vcpu%d p = %d b = %d/n", svc->vcpu->vcpu_id, svc->new_param.period, svc->new_param.budget);
    svc->period = svc->new_param.period;
    svc->budget = svc->new_param.budget;

}
static int
_old_vcpus_inactive(void)
{
    struct list_head* iter;
    list_for_each(iter, &rtds_mc.old_vcpus)
    {
        struct rt_vcpu* svc = __type_elem(iter);
        if(svc->active == 1)
            return 0;
    }
    return 1;
}

static int
_changed_vcpus_inactive(void)
{
    struct list_head* iter;
    list_for_each(iter, &rtds_mc.changed_vcpus)
    {
        struct rt_vcpu* svc = __type_elem(iter);
        if(svc->active == 1)
            return 0;
    }
    return 1;
}

/*
 * Checks if all changed vcpus are released
 * by checking if the type queue is empty.
 * If a protocol is synchronous, a changed
 * vcpu will be taken off from type queue.
 * This function should only be used with sync == 1
 */
static int
_changed_vcpus_released(void)
{
    return list_empty(&rtds_mc.changed_vcpus)
}


/* need to update deadline before activating */
static void
__activate_vcpu(const struct scheduler *ops, struct rt_vcpu* svc)
{
    if( svc->active == 0 )
    {
        svc->active = 1;
        if( !(svc->flags & RTDS_delayed_runq_add) )
            __runq_insert(ops, svc);
        __replq_insert(ops, svc);
        printk("activate vcpu%d\n",svc->vcpu->vcpu_id); 
    }
}

/*
 * deactivate a single vcpu and take it off the queues
 * if it is delay_add mark it !active so it won't be added
 * back to queue in context_saved()
 */
static void
__deactivate_vcpu(const struct scheduler *ops, struct rt_vcpu* svc)
{
    if( svc->active == 1 )
    {    
        svc->active = 0; /* used in context_saved() */

        if ( __vcpu_on_q(svc) )
            __q_remove(svc);

        if( __vcpu_on_replq(svc) )
            __replq_remove(ops,svc);

        printk("de-activate vcpu%d\n",svc->vcpu->vcpu_id);
        
    }
}

/*
 * activate all new vcpus that are !active
 * some of the new vcpus might be disabled 
 * long time ago so update deadline accordingly
 */
static void
_activate_all_new_vcpus(const struct scheduler *ops)
{
    struct list_head* iter, *temp;
    s_time_t now = NOW();

    printk("activating all new vpus\n");
    list_for_each_safe(iter, temp, &rtds_mc.new_vcpus)
    {
        struct rt_vcpu* svc = __type_elem(iter);

        if(svc->active == 0)
        {
            struct rt_vcpu *svc = __type_elem(iter);
            if ( now >= svc->cur_deadline )
                rt_update_deadline(now, svc);
            else
                svc->cur_budget = svc->budget;

            __type_remove(svc);
            __activate_vcpu(ops, svc);
        }
    }
}

/*
 * activate all unchanged vcpus that are !active
 * some of the new vcpus might be disabled 
 * long time ago so update deadline accordingly
 */
static void
_activate_all_unchanged_vcpus(const struct scheduler *ops)
{
    struct list_head *iter, *temp;
    s_time_t now = NOW();

    printk("activating all unchanged vpus\n");
    list_for_each_safe(iter, temp, &rtds_mc.unchanged_vcpus)
    {
        struct rt_vcpu* svc = __type_elem(iter);

        if(svc->active == 0)
        {
            struct rt_vcpu *svc = __type_elem(iter);
            if ( now >= svc->cur_deadline )
                rt_update_deadline(now, svc);

            __type_remove(svc);
            __activate_vcpu(ops, svc);
        }
    }
}


/*
 * activate all changed vcpus that are deactivated
 * update param before activation. Also refill budget
 */
static void
_activate_all_changed_vcpus(const struct scheduler *ops)
{
    struct list_head* iter, *temp;
    s_time_t now = NOW();

    printk("activating all changed vpus\n");
    list_for_each_safe(iter, temp, &rtds_mc.changed_vcpus)
    {
        struct rt_vcpu* svc = __type_elem(iter);

        if(svc->active == 0 && svc->mode == 0)
        {
            struct rt_vcpu *svc = __type_elem(iter);
 
            _update_changed_vcpu(svc); 

            if ( now >= svc->cur_deadline )
                rt_update_deadline(now, svc);
            else
                svc->cur_budget = svc->budget;

            __type_remove(svc);

            __activate_vcpu(ops, svc);
        }
    }

}

/*
 * Init/Free related code
 */
static int
rt_init(struct scheduler *ops)
{
    struct rt_private *prv = xzalloc(struct rt_private);

    printk("Initializing RTDS scheduler\n"
           "WARNING: This is experimental software in development.\n"
           "Use at your own risk.\n");

    if ( prv == NULL )
        return -ENOMEM;

    ASSERT( _cpumask_scratch == NULL || nr_rt_ops > 0 );

    if ( !_cpumask_scratch )
    {
        _cpumask_scratch = xmalloc_array(cpumask_var_t, nr_cpu_ids);
        if ( !_cpumask_scratch )
            goto no_mem;
    }
    nr_rt_ops++;

    spin_lock_init(&prv->lock);
    INIT_LIST_HEAD(&prv->sdom);
    INIT_LIST_HEAD(&prv->runq);
    INIT_LIST_HEAD(&prv->depletedq);
    INIT_LIST_HEAD(&prv->replq);

    /* mode change */
    INIT_LIST_HEAD(&rtds_mc.old_vcpus);
    INIT_LIST_HEAD(&rtds_mc.new_vcpus);
    INIT_LIST_HEAD(&rtds_mc.unchanged_vcpus);
    INIT_LIST_HEAD(&rtds_mc.changed_vcpus);

    cpumask_clear(&prv->tickled);

    ops->sched_data = prv;

    /* 
     * The timer initialization will happen later when 
     * the first pcpu is added to this pool in alloc_pdata
     */
    prv->repl_timer = NULL;

    return 0;

 no_mem:
    xfree(prv);
    return -ENOMEM;
}

static void
rt_deinit(const struct scheduler *ops)
{
    struct rt_private *prv = rt_priv(ops);

    ASSERT( _cpumask_scratch && nr_rt_ops > 0 );

    if ( (--nr_rt_ops) == 0 )
    {
        xfree(_cpumask_scratch);
        _cpumask_scratch = NULL;
    }

    kill_timer(prv->repl_timer);
    xfree(prv->repl_timer);

    xfree(prv);
}

/*
 * Point per_cpu spinlock to the global system lock;
 * All cpu have same global system lock
 */
static void *
rt_alloc_pdata(const struct scheduler *ops, int cpu)
{
    struct rt_private *prv = rt_priv(ops);
    unsigned long flags;

    spin_lock_irqsave(&prv->lock, flags);
    per_cpu(schedule_data, cpu).schedule_lock = &prv->lock;
    spin_unlock_irqrestore(&prv->lock, flags);

    if ( !alloc_cpumask_var(&_cpumask_scratch[cpu]) )
        return NULL;

    if( prv->repl_timer == NULL )
    {   
        /* allocate the timer on the first cpu of this pool */
        prv->repl_timer = xzalloc(struct timer);

        if(prv->repl_timer == NULL )
            return NULL;

        init_timer(prv->repl_timer, repl_handler, (void *)ops, cpu);
    }

    /* 1 indicates alloc. succeed in schedule.c */
    return (void *)1;
}

static void
rt_free_pdata(const struct scheduler *ops, void *pcpu, int cpu)
{
    struct rt_private *prv = rt_priv(ops);
    struct schedule_data *sd = &per_cpu(schedule_data, cpu);
    unsigned long flags;

    spin_lock_irqsave(&prv->lock, flags);

    /* Move spinlock back to the default lock */
    ASSERT(sd->schedule_lock == &prv->lock);
    ASSERT(!spin_is_locked(&sd->_lock));
    sd->schedule_lock = &sd->_lock;

    spin_unlock_irqrestore(&prv->lock, flags);

    free_cpumask_var(_cpumask_scratch[cpu]);
}

static void *
rt_alloc_domdata(const struct scheduler *ops, struct domain *dom)
{
    unsigned long flags;
    struct rt_dom *sdom;
    struct rt_private * prv = rt_priv(ops);

    sdom = xzalloc(struct rt_dom);
    if ( sdom == NULL )
        return NULL;

    INIT_LIST_HEAD(&sdom->sdom_elem);
    sdom->dom = dom;

    /* spinlock here to insert the dom */
    spin_lock_irqsave(&prv->lock, flags);
    list_add_tail(&sdom->sdom_elem, &(prv->sdom));
    spin_unlock_irqrestore(&prv->lock, flags);

    return sdom;
}

static void
rt_free_domdata(const struct scheduler *ops, void *data)
{
    unsigned long flags;
    struct rt_dom *sdom = data;
    struct rt_private *prv = rt_priv(ops);

    spin_lock_irqsave(&prv->lock, flags);
    list_del_init(&sdom->sdom_elem);
    spin_unlock_irqrestore(&prv->lock, flags);
    xfree(data);
}

static int
rt_dom_init(const struct scheduler *ops, struct domain *dom)
{
    struct rt_dom *sdom;

    /* IDLE Domain does not link on rt_private */
    if ( is_idle_domain(dom) )
        return 0;

    sdom = rt_alloc_domdata(ops, dom);
    if ( sdom == NULL )
        return -ENOMEM;

    dom->sched_priv = sdom;

    return 0;
}

static void
rt_dom_destroy(const struct scheduler *ops, struct domain *dom)
{
    rt_free_domdata(ops, rt_dom(dom));
}

static void *
rt_alloc_vdata(const struct scheduler *ops, struct vcpu *vc, void *dd)
{
    struct rt_vcpu *svc;

    /* Allocate per-VCPU info */
    svc = xzalloc(struct rt_vcpu);
    if ( svc == NULL )
        return NULL;

    INIT_LIST_HEAD(&svc->q_elem);
    INIT_LIST_HEAD(&svc->replq_elem);

    /* mode change */
    INIT_LIST_HEAD(&svc->type_elem);

    svc->flags = 0U;
    svc->sdom = dd;
    svc->vcpu = vc;
    svc->last_start = 0;

    svc->period = RTDS_DEFAULT_PERIOD;
    if ( !is_idle_vcpu(vc) )
    {
        svc->budget = RTDS_DEFAULT_BUDGET;
        svc->active = 1; 
    }

    SCHED_STAT_CRANK(vcpu_alloc);

    return svc;
}

static void
rt_free_vdata(const struct scheduler *ops, void *priv)
{
    struct rt_vcpu *svc = priv;

    xfree(svc);
}

/*
 * It is called in sched_move_domain() and sched_init_vcpu
 * in schedule.c
 * When move a domain to a new cpupool.
 * It inserts vcpus of moving domain to the scheduler's RunQ in
 * dest. cpupool.
 */
static void
rt_vcpu_insert(const struct scheduler *ops, struct vcpu *vc)
{
    struct rt_vcpu *svc = rt_vcpu(vc);
    s_time_t now = NOW();
    spinlock_t *lock;

    BUG_ON( is_idle_vcpu(vc) );

    lock = vcpu_schedule_lock_irq(vc);
    if ( now >= svc->cur_deadline )
        rt_update_deadline(now, svc);

    if ( !__vcpu_on_q(svc) && vcpu_runnable(vc) )
    {
        __replq_insert(ops, svc);
 
        if( !vc->is_running)
            __runq_insert(ops, svc);
    }
    vcpu_schedule_unlock_irq(lock, vc);

    SCHED_STAT_CRANK(vcpu_insert);
}

/*
 * Remove rt_vcpu svc from the old scheduler in source cpupool.
 */
static void
rt_vcpu_remove(const struct scheduler *ops, struct vcpu *vc)
{
    struct rt_vcpu * const svc = rt_vcpu(vc);
    struct rt_dom * const sdom = svc->sdom;
    spinlock_t *lock;

    SCHED_STAT_CRANK(vcpu_remove);

    BUG_ON( sdom == NULL );

    lock = vcpu_schedule_lock_irq(vc);
    if ( __vcpu_on_q(svc) )
        __q_remove(svc);

    if( vcpu_on_replq(svc) )
        __replq_remove(ops,svc);

    vcpu_schedule_unlock_irq(lock, vc);
}

/*
 * Pick a valid CPU for the vcpu vc
 * Valid CPU of a vcpu is intesection of vcpu's affinity
 * and available cpus
 */
static int
rt_cpu_pick(const struct scheduler *ops, struct vcpu *vc)
{
    cpumask_t cpus;
    cpumask_t *online;
    int cpu;

    online = cpupool_domain_cpumask(vc->domain);
    cpumask_and(&cpus, online, vc->cpu_hard_affinity);

    cpu = cpumask_test_cpu(vc->processor, &cpus)
            ? vc->processor
            : cpumask_cycle(vc->processor, &cpus);
    ASSERT( !cpumask_empty(&cpus) && cpumask_test_cpu(cpu, &cpus) );

    return cpu;
}

/*
 * Burn budget in nanosecond granularity
 */
static void
burn_budget(const struct scheduler *ops, struct rt_vcpu *svc, s_time_t now)
{
    s_time_t delta;

    /* don't burn budget for idle VCPU */
    if ( is_idle_vcpu(svc->vcpu) )
        return;

    /* burn at nanoseconds level */
    delta = now - svc->last_start;
    /*
     * delta < 0 only happens in nested virtualization;
     * TODO: how should we handle delta < 0 in a better way?
     */
    if ( delta < 0 )
    {
        printk("%s, ATTENTION: now is behind last_start! delta=%"PRI_stime"\n",
                __func__, delta);
        svc->last_start = now;
        return;
    }

    svc->cur_budget -= delta;

    if ( svc->cur_budget < 0 )
        svc->cur_budget = 0;

    /* TRACE */
    {
        struct {
            unsigned dom:16, vcpu:16;
            unsigned cur_budget_lo;
            unsigned cur_budget_hi;
            int delta;
        } d;
        d.dom = svc->vcpu->domain->domain_id;
        d.vcpu = svc->vcpu->vcpu_id;
        d.cur_budget_lo = (unsigned) svc->cur_budget;
        d.cur_budget_hi = (unsigned) (svc->cur_budget >> 32);
        d.delta = delta;
        trace_var(TRC_RTDS_BUDGET_BURN, 1,
                  sizeof(d),
                  (unsigned char *) &d);
    }
}

/*
 * RunQ is sorted. Pick first one within cpumask. If no one, return NULL
 * lock is grabbed before calling this function
 */
static struct rt_vcpu *
__runq_pick(const struct scheduler *ops, const cpumask_t *mask)
{
    struct list_head *runq = rt_runq(ops);
    struct list_head *iter;
    struct rt_vcpu *svc = NULL;
    struct rt_vcpu *iter_svc = NULL;
    cpumask_t cpu_common;
    cpumask_t *online;

    list_for_each(iter, runq)
    {
        iter_svc = __q_elem(iter);

        /* mask cpu_hard_affinity & cpupool & mask */
        online = cpupool_domain_cpumask(iter_svc->vcpu->domain);
        cpumask_and(&cpu_common, online, iter_svc->vcpu->cpu_hard_affinity);
        cpumask_and(&cpu_common, mask, &cpu_common);
        if ( cpumask_empty(&cpu_common) )
            continue;

        ASSERT( iter_svc->cur_budget > 0 );

        svc = iter_svc;
        break;
    }

    /* TRACE */
    {
        if( svc != NULL )
        {
            struct {
                unsigned dom:16, vcpu:16;
                unsigned cur_deadline_lo, cur_deadline_hi;
                unsigned cur_budget_lo, cur_budget_hi;
            } d;
            d.dom = svc->vcpu->domain->domain_id;
            d.vcpu = svc->vcpu->vcpu_id;
            d.cur_deadline_lo = (unsigned) svc->cur_deadline;
            d.cur_deadline_hi = (unsigned) (svc->cur_deadline >> 32);
            d.cur_budget_lo = (unsigned) svc->cur_budget;
            d.cur_budget_hi = (unsigned) (svc->cur_budget >> 32);
            trace_var(TRC_RTDS_RUNQ_PICK, 1,
                      sizeof(d),
                      (unsigned char *) &d);
        }
        else
            trace_var(TRC_RTDS_RUNQ_PICK, 1, 0, NULL);
    }

    return svc;
}

/*
 * when mode change is finished, it cleans up
 * all the lists and states
 */
static void
mode_change_over(const struct scheduler *ops)
{
    struct list_head *iter, *temp;
    struct list_head *lists[4] = {
        &rtds_mc.old_vcpus,
        &rtds_mc.new_vcpus,
        &rtds_mc.changed_vcpus,
        &rtds_mc.unchanged_vcpus
    };
    int i;
    printk("cleaning up:\n");
    for( i = 0; i< 4; i++)
    {
        list_for_each_safe( iter, temp, lists[i] )
        {
            struct rt_vcpu* svc = __type_elem(iter);
            rt_dump_vcpu(ops, svc);
        /* taken off runq/replq inside */
            __type_remove(svc);
        }

    }
    rtds_mc.in_trans = 0;
    printk("mc took %"PRI_stime"\n", NOW() - rtds_mc.recv); 
}

/*
 * schedule function for rt scheduler.
 * The lock is already grabbed in schedule.c, no need to lock here
 */
static struct task_slice
rt_schedule(const struct scheduler *ops, s_time_t now, bool_t tasklet_work_scheduled)
{
    const int cpu = smp_processor_id();
    struct rt_private *prv = rt_priv(ops);
    struct rt_vcpu *const scurr = rt_vcpu(current);
    struct rt_vcpu *snext = NULL;
    struct task_slice ret = { .migrated = 0 };
    static int count = 0;
    struct list_head *iter,*temp;

    /* clear ticked bit now that we've been scheduled */
    cpumask_clear_cpu(cpu, &prv->tickled);

    /* burn_budget would return for IDLE VCPU */
    burn_budget(ops, scurr, now);

    if(rtds_mc.in_trans)
    {    
        printk("rt_sched:\nscurr = vcpu%d, %sactive type=%d %sidle bgt=%"PRI_stime" now=%"PRI_stime"\n", current->vcpu_id, scurr->active == 1? "":" not", scurr->type, is_idle_vcpu(scurr->vcpu)? "":"not ", scurr->budget, now);
        count++;
        if(count == 20)
            rtds_mc.in_trans = 0;
    }
    if ( tasklet_work_scheduled )
    {
        snext = rt_vcpu(idle_vcpu[cpu]);
        if(rtds_mc.in_trans)
            printk("tasklet shit\n");
    }
    else
    {
        /* in transition */
        if( rtds_mc.in_trans )
        {
            printk("in trans..\n");
/* off-set for disabling old/changed vcpus */
            if(now - rtds_mc.recv >= rtds_mc.info.ofst_old)
            {
                printk("disabling old..\n");
                list_for_each_safe( iter, temp, &rtds_mc.old_vcpus )
                {
                    struct rt_vcpu* svc = __type_elem(iter);
                    rt_dump_vcpu(ops, svc);

                    /*
                     * only disable it if it has no budget
                     * if it is alseep
                     */
                    if( svc->cur_budget == 0 || ( !vcpu_runnable(svc->vcpu) ) )
                    {   /* taken off runq/replq inside */
                        __deactivate_vcpu(ops, svc);
                        /* taken off type q if its old */
                        __type_remove(svc);
                    }
                }

                printk("disabling changed..\n");
                list_for_each_safe( iter, temp, &rtds_mc.changed_vcpus )
                {
                    struct rt_vcpu* svc = __type_elem(iter);
                    rt_dump_vcpu(ops, svc);

                    /*
                     * only disable it if it has no budget
                     * if it is alseep
                     * and if it's current active
                     */
                    if( svc->active == 1 && 
                        ( svc->cur_budget == 0 || ( !vcpu_runnable(svc->vcpu) ) ) )
                    {
                        if(rtds_mc.info.sync == 0)
                        {
                            /* release the updated vcpu */
                            _update_changed_vcpu(svc);
                            /* stop tracking it */
                            __type_remove(svc);

                            if ( now >= svc->cur_deadline )
                            {
                                rt_update_deadline(now, svc);
                                if( __vcpu_on_q(svc) )
                                {
                                    __q_remove(svc);
                                    __runq_insert(ops, svc);
                                }
                                __replq_remove(ops, svc);
                                __replq_insert(ops, svc);
                            }
                        }
                        else
                        {
                            /* temperarily disable this vcpu */
                            __deactivate_vcpu(ops, svc);
                            /* 
                             * Add the flag ahead so re-activating won't add
                             * to runq again in activate().
                             */
                            if ( svc == scurr && !is_idle_vcpu(current) &&
                                    vcpu_runnable(current) )
                                set_bit(__RTDS_delayed_runq_add, &scurr->flags);

                            /* don't remove from type list for later activation */
                        }
                    }
                }
             
            }
            /* guard to disable all at once for idle time proto */
            else if ( 0 &&_runq_empty(ops) && scurr->cur_budget == 0)
            {
                printk("guard for idle time satisfied\n");
                
            }
            else
                printk("not ready to disable old.%"PRI_stime" %"PRI_stime"\n"
                    ,now - rtds_mc.recv, rtds_mc.info.ofst_old);



/* periodicity for unchanged tasks */
            if( rtds_mc.info.peri == 0 )
            {
                printk("disabling unchanged..\n");
                list_for_each_safe( iter, temp, &rtds_mc.unchanged_vcpus )
                {
                    struct rt_vcpu* svc = __type_elem(iter);
                    rt_dump_vcpu(ops, svc);

                    if( svc->cur_budget == 0 || ( !__vcpu_on_q(svc) ) )
                    {   /* taken off runq/replq inside */
                        __deactivate_vcpu(ops, svc);
                    }
                }
            }

/* synchronous */
            if( rtds_mc.info.sync == 1 )
            {
                if( _old_vcpus_inactive() && _changed_vcpus_inactive() )
                {
                    _activate_all_new_vcpus(ops);

                    if( rtds_mc.info.peri == 0 )
                        _activate_all_unchanged_vcpus(ops);

                    _activate_all_changed_vcpus(ops);
                    mode_change_over(ops);
                    count = 0;
                    printk("sync satisfied, mc finished\n");
                }
                else
                    printk("sync not satisfied\n");
            }
            else
            {
                _activate_all_new_vcpus(ops);

                if( rtds_mc.info.peri == 0 )
                    _activate_all_unchanged_vcpus(ops);

                if(changed_vcpus_released())
                
                mode_change_over(ops);
                count = 0;
                printk("async mc finished...\n");
            }
        }
/* mode change over */

        snext = __runq_pick(ops, cpumask_of(cpu));
        if ( snext == NULL )
        {
            snext = rt_vcpu(idle_vcpu[cpu]);
            if(rtds_mc.in_trans)
                printk("nothing available\n");
        }

        /* if scurr has higher priority and budget, still pick scurr */
        if ( !is_idle_vcpu(current) &&
            vcpu_runnable(current) &&
            scurr->cur_budget > 0 &&
            scurr->active == 1 && /* still run scurr if active */
            ( is_idle_vcpu(snext->vcpu) ||
            scurr->cur_deadline <= snext->cur_deadline ) )
            snext = scurr;

        if(rtds_mc.in_trans)
        {
            if( is_idle_vcpu(snext->vcpu) )
                printk("snext: idle\n");
            else
                printk("snext: vcpu%d\n",snext->vcpu->vcpu_id);
        }
    }

    if ( snext != scurr &&
         !is_idle_vcpu(current) &&
         vcpu_runnable(current) )
        set_bit(__RTDS_delayed_runq_add, &scurr->flags);

    snext->last_start = now;

    ret.time =  -1; /* if an idle vcpu is picked */ 
    if ( !is_idle_vcpu(snext->vcpu) )
    {
        if ( snext != scurr )
        {
            __q_remove(snext);
            set_bit(__RTDS_scheduled, &snext->flags);
        }
        if ( snext->vcpu->processor != cpu )
        {
            snext->vcpu->processor = cpu;
            ret.migrated = 1;
        }
        ret.time = snext->budget; /* invoke the scheduler next time */
    }

    ret.task = snext->vcpu;

    if(rtds_mc.in_trans)
    {
        printk("rt.time = %"PRI_stime"\n",snext->budget);
        printk("picked vcpu%d",snext->vcpu->vcpu_id);
    }
    /* TRACE */
    {
        struct {
            unsigned dom:16,vcpu:16;
            unsigned cur_deadline_lo, cur_deadline_hi;
            unsigned cur_budget_lo, cur_budget_hi;
        } d;
        d.dom = snext->vcpu->domain->domain_id;
        d.vcpu = snext->vcpu->vcpu_id;
        d.cur_deadline_lo = (unsigned) snext->cur_deadline;
        d.cur_deadline_hi = (unsigned) (snext->cur_deadline >> 32);
        d.cur_budget_lo = (unsigned) snext->cur_budget;
        d.cur_budget_hi = (unsigned) (snext->cur_budget >> 32);
        trace_var(TRC_RTDS_SCHED_TASKLET, 1,
                  sizeof(d),
                  (unsigned char *)&d);
    }

    return ret;
}

/*
 * Remove VCPU from RunQ
 * The lock is already grabbed in schedule.c, no need to lock here
 */
static void
rt_vcpu_sleep(const struct scheduler *ops, struct vcpu *vc)
{
    struct rt_vcpu * const svc = rt_vcpu(vc);

    BUG_ON( is_idle_vcpu(vc) );
    SCHED_STAT_CRANK(vcpu_sleep);

    if ( curr_on_cpu(vc->processor) == vc )
        cpu_raise_softirq(vc->processor, SCHEDULE_SOFTIRQ);
    else if ( __vcpu_on_q(svc) )
    {
        __q_remove(svc);
        __replq_remove(ops, svc);
    }
    else if ( svc->flags & RTDS_delayed_runq_add )
        clear_bit(__RTDS_delayed_runq_add, &svc->flags);
}

/*
 * Pick a cpu where to run a vcpu,
 * possibly kicking out the vcpu running there
 * Called by wake() and context_saved()
 * We have a running candidate here, the kick logic is:
 * Among all the cpus that are within the cpu affinity
 * 1) if the new->cpu is idle, kick it. This could benefit cache hit
 * 2) if there are any idle vcpu, kick it.
 * 3) now all pcpus are busy;
 *    among all the running vcpus, pick lowest priority one
 *    if snext has higher priority, kick it.
 *
 * TODO:
 * 1) what if these two vcpus belongs to the same domain?
 *    replace a vcpu belonging to the same domain introduces more overhead
 *
 * lock is grabbed before calling this function
 */
static void
runq_tickle(const struct scheduler *ops, struct rt_vcpu *new)
{
    struct rt_private *prv = rt_priv(ops);
    struct rt_vcpu *latest_deadline_vcpu = NULL; /* lowest priority */
    struct rt_vcpu *iter_svc;
    struct vcpu *iter_vc;
    int cpu = 0, cpu_to_tickle = 0;
    cpumask_t not_tickled;
    cpumask_t *online;

    if ( new == NULL || is_idle_vcpu(new->vcpu) )
        return;

    online = cpupool_domain_cpumask(new->vcpu->domain);
    cpumask_and(&not_tickled, online, new->vcpu->cpu_hard_affinity);
    cpumask_andnot(&not_tickled, &not_tickled, &prv->tickled);

    /* 1) if new's previous cpu is idle, kick it for cache benefit */
    if ( is_idle_vcpu(curr_on_cpu(new->vcpu->processor)) )
    {
        cpu_to_tickle = new->vcpu->processor;
        goto out;
    }

    /* 2) if there are any idle pcpu, kick it */
    /* The same loop also find the one with lowest priority */
    for_each_cpu(cpu, &not_tickled)
    {
        iter_vc = curr_on_cpu(cpu);
        if ( is_idle_vcpu(iter_vc) )
        {
            cpu_to_tickle = cpu;
            goto out;
        }
        iter_svc = rt_vcpu(iter_vc);
        if ( latest_deadline_vcpu == NULL ||
             iter_svc->cur_deadline > latest_deadline_vcpu->cur_deadline )
            latest_deadline_vcpu = iter_svc;
    }

    /* 3) candicate has higher priority, kick out lowest priority vcpu */
    if ( latest_deadline_vcpu != NULL &&
         new->cur_deadline < latest_deadline_vcpu->cur_deadline )
    {
        cpu_to_tickle = latest_deadline_vcpu->vcpu->processor;
        goto out;
    }

    /* didn't tickle any cpu */
    SCHED_STAT_CRANK(tickle_idlers_none);
    return;
out:
    /* TRACE */
    {
        struct {
            unsigned cpu:16, pad:16;
        } d;
        d.cpu = cpu_to_tickle;
        d.pad = 0;
        trace_var(TRC_RTDS_TICKLE, 0,
                  sizeof(d),
                  (unsigned char *)&d);
    }

    cpumask_set_cpu(cpu_to_tickle, &prv->tickled);
    SCHED_STAT_CRANK(tickle_idlers_some);
    cpu_raise_softirq(cpu_to_tickle, SCHEDULE_SOFTIRQ);
    return;
}

/*
 * Should always wake up runnable vcpu, put it back to RunQ.
 * Check priority to raise interrupt
 * The lock is already grabbed in schedule.c, no need to lock here
 * TODO: what if these two vcpus belongs to the same domain?
 */
static void
rt_vcpu_wake(const struct scheduler *ops, struct vcpu *vc)
{
    struct rt_vcpu * const svc = rt_vcpu(vc);
    s_time_t now = NOW();

    BUG_ON( is_idle_vcpu(vc) );

    if ( unlikely(curr_on_cpu(vc->processor) == vc) )
    {
        SCHED_STAT_CRANK(vcpu_wake_running);
        return;
    }

    /* on RunQ/DepletedQ, just update info is ok */
    if ( unlikely(__vcpu_on_q(svc)) )
    {
        SCHED_STAT_CRANK(vcpu_wake_onrunq);
        return;
    }

    if ( likely(vcpu_runnable(vc)) )
        SCHED_STAT_CRANK(vcpu_wake_runnable);
    else
        SCHED_STAT_CRANK(vcpu_wake_not_runnable);

    if( !svc->active )
    {
        printk("vcpu%d wake up inactive\n", vc->vcpu_id);
        return;
    }

    /*
     * If a deadline passed while svc was asleep/blocked, we need new
     * scheduling parameters ( a new deadline and full budget), and
     * also a new replenishment event
     */
    if ( now >= svc->cur_deadline)
    {   
        rt_update_deadline(now, svc);
        __replq_remove(ops, svc);
    }

    if( !vcpu_on_replq(svc) )
        __replq_insert(ops, svc);
    /* If context hasn't been saved for this vcpu yet, we can't put it on
     * the Runqueue/DepletedQ. Instead, we set a flag so that it will be
     * put on the Runqueue/DepletedQ after the context has been saved.
     */
    if ( unlikely(svc->flags & RTDS_scheduled) )
    {
        set_bit(__RTDS_delayed_runq_add, &svc->flags);
        return;
    }

    /* insert svc to runq/depletedq because svc is not in queue now */
    __runq_insert(ops, svc);

    runq_tickle(ops, svc);
}

/*
 * scurr has finished context switch, insert it back to the RunQ,
 * and then pick the highest priority vcpu from runq to run
 */
static void
rt_context_saved(const struct scheduler *ops, struct vcpu *vc)
{
    struct rt_vcpu *svc = rt_vcpu(vc);
    spinlock_t *lock = vcpu_schedule_lock_irq(vc);

    clear_bit(__RTDS_scheduled, &svc->flags);
    /* not insert idle vcpu to runq */
    if ( is_idle_vcpu(vc) )
        goto out;

    if ( test_and_clear_bit(__RTDS_delayed_runq_add, &svc->flags) &&
         likely(vcpu_runnable(vc)) && svc->active == 1 )
    {
        __runq_insert(ops, svc);
        runq_tickle(ops, svc);
    }
    else
        __replq_remove(ops, svc);
out:
    vcpu_schedule_unlock_irq(lock, vc);
}

/*
 * set/get each vcpu info of each domain
 */
static int
rt_dom_cntl(
    const struct scheduler *ops,
    struct domain *d,
    struct xen_domctl_scheduler_op *op)
{
    struct rt_private *prv = rt_priv(ops);
    struct rt_vcpu *svc;
    struct vcpu *v;
    unsigned long flags;
    int rc = 0;
    struct list_head *iter;
    xen_domctl_schedparam_t local_params;
    mode_change_info_t* mc;
    uint32_t index = 0;
    uint32_t len;


    switch ( op->cmd )
    {
    case XEN_DOMCTL_SCHEDOP_getinfo:
        if ( d->max_vcpus > 0 )
        {
            spin_lock_irqsave(&prv->lock, flags);
            svc = rt_vcpu(d->vcpu[0]);
            op->u.rtds.period = svc->period / MICROSECS(1);
            op->u.rtds.budget = svc->budget / MICROSECS(1);
            spin_unlock_irqrestore(&prv->lock, flags);
        }
        else
        {
            /* If we don't have vcpus yet, let's just return the defaults. */
            op->u.rtds.period = RTDS_DEFAULT_PERIOD;
            op->u.rtds.budget = RTDS_DEFAULT_BUDGET;
        }
        break;
    case XEN_DOMCTL_SCHEDOP_putinfo:
        if ( op->u.rtds.period == 0 || op->u.rtds.budget == 0 )
        {
            rc = -EINVAL;
            break;
        }
        spin_lock_irqsave(&prv->lock, flags);
        for_each_vcpu ( d, v )
        {
            svc = rt_vcpu(v);
            svc->period = MICROSECS(op->u.rtds.period); /* transfer to nanosec */
            svc->budget = MICROSECS(op->u.rtds.budget);
        }
        spin_unlock_irqrestore(&prv->lock, flags);
        break;

    case XEN_DOMCTL_SCHEDOP_putMC: 
        printk("rtds mode change info:\n");
        spin_lock_irqsave(&prv->lock, flags);
        mc = &(op->u.mode_change.info);
        len = mc->nr_new + mc->nr_old + mc->nr_changed + mc->nr_unchanged;
        printk("total %d vcpus\n", len);
        for ( index = 0; index < len; index++ )
        {
            if ( copy_from_guest_offset(&local_params, op->u.mode_change.params, index,1) )
            {
                rc = -EFAULT;
                goto out;
            }

            if( local_params.vcpuid >= d->max_vcpus ||
                    d->vcpu[local_params.vcpuid] == NULL ) 
            {
                rc = -EINVAL;
                spin_unlock_irqrestore(&prv->lock, flags);
                break;
            }
            svc = rt_vcpu(d->vcpu[local_params.vcpuid]);
            svc->new_param = local_params.rtds;
            svc->active = 1;
            switch (local_params.type)
            {
                case OLD:
                    svc->type = OLD;
                    printk("vcpu%d is old\n", svc->vcpu->vcpu_id);
                    list_add_tail(&svc->type_elem, &rtds_mc.old_vcpus);
                    break;
                case NEW:
                    svc->type = NEW;
                    svc->active = 0;
                    printk("vcpu%d is new ", svc->vcpu->vcpu_id);
                
                    svc->period = svc->new_param.period;
                    svc->budget = svc->new_param.budget;
                    printk(" -p%d -b%d\n",svc->new_param.period,svc->new_param.budget);

                    list_add_tail(&svc->type_elem, &rtds_mc.new_vcpus);
                    break;
                case CHANGED:
                    svc->type = CHANGED;
                    svc->mode = 0;
                    printk("vcpu%d is changed", svc->vcpu->vcpu_id);
                    printk(" -p%d -b%d\n",svc->new_param.period,svc->new_param.budget);

                    list_add_tail(&svc->type_elem, &rtds_mc.changed_vcpus);
                    break;
                case UNCHANGED:
                    svc->type = UNCHANGED;
                    printk("vcpu%d is unchanged\n", svc->vcpu->vcpu_id);
                    list_add_tail(&svc->type_elem, &rtds_mc.unchanged_vcpus);
                    break;

            }
        }

        rtds_mc.info = *mc;

        printk("\noff_set_old: %"PRIu64"\n", rtds_mc.info.ofst_old);
        printk("off_set_new: %"PRIu64"\n", rtds_mc.info.ofst_new);

        printk("This protocol is ");
        printk(rtds_mc.info.sync == 1? "synchronous ":
            "asynchronous ");

        printk(rtds_mc.info.peri == 1? "with periodicity\n":
            "without periodicity\n");

        
        printk("old vcpu info:\n");
        list_for_each( iter, &rtds_mc.old_vcpus )
        {
            svc = __type_elem(iter);
            printk("vcpu%d ",svc->vcpu->vcpu_id);
        }

        printk("\nnew vcpu info:\n");
        list_for_each( iter, &rtds_mc.new_vcpus )
        {
            svc = __type_elem(iter);
            printk("vcpu%d ",svc->vcpu->vcpu_id);
        }

        printk("\nunchanged vcpu info:\n");
        list_for_each( iter, &rtds_mc.unchanged_vcpus )
        {
            svc = __type_elem(iter);
            printk("vcpu%d ",svc->vcpu->vcpu_id);
        }
 
        printk("\nchanged vcpu info:\n");
        list_for_each( iter, &rtds_mc.changed_vcpus )
        {
            svc = __type_elem(iter);
            printk("vcpu%d ",svc->vcpu->vcpu_id);
        }

        printk("\n");

        rtds_mc.in_trans = 1;
        rtds_mc.recv = NOW();

        /* invoke scheduler now */
        cpu_raise_softirq(current->processor, SCHEDULE_SOFTIRQ);
    out:
        spin_unlock_irqrestore(&prv->lock, flags);
        break;
    }
    return rc;
}

/*
 * The replenishment timer handler picks vcpus
 * from the replq and does the actual replenishment
 */
static void repl_handler(void *data){
    unsigned long flags;
    s_time_t now = NOW();
    struct scheduler *ops = data; 
    struct rt_private *prv = rt_priv(ops);
    struct list_head *replq = rt_replq(ops);
    struct timer *repl_timer = prv->repl_timer;
    struct list_head *iter, *tmp;
    struct rt_vcpu *svc = NULL;

    spin_lock_irqsave(&prv->lock, flags);

    stop_timer(repl_timer);

    list_for_each_safe(iter, tmp, replq)
    {
        svc = replq_elem(iter);

        if ( now < svc->cur_deadline )
            break;

        rt_update_deadline(now, svc);

        /*
         * when the replenishment happens
         * svc is either on a pcpu or on
         * runq/depletedq
         */
        if( __vcpu_on_q(svc) )
        {
            /* put back to runq */
            __q_remove(svc);
            __runq_insert(ops, svc);
        }

        /* 
         * tickle regardless where it's at 
         * because a running vcpu could have
         * a later deadline than others after
         * replenishment
         */
        runq_tickle(ops, svc);

        /* 
         * update replenishment event queue
         * without reprogramming the timer
         */
        deadline_queue_remove(replq, &svc->replq_elem);
        deadline_queue_insert(&replq_elem, svc, &svc->replq_elem, replq);
    }

    /* 
     * use the vcpu that's on the top
     * or else don't program the timer
     */
    if( !list_empty(replq) )
        set_timer(repl_timer, replq_elem(replq->next)->cur_deadline);

    spin_unlock_irqrestore(&prv->lock, flags);
}

static struct rt_private _rt_priv;

static const struct scheduler sched_rtds_def = {
    .name           = "SMP RTDS Scheduler",
    .opt_name       = "rtds",
    .sched_id       = XEN_SCHEDULER_RTDS,
    .sched_data     = &_rt_priv,

    .dump_cpu_state = rt_dump_pcpu,
    .dump_settings  = rt_dump,
    .init           = rt_init,
    .deinit         = rt_deinit,
    .alloc_pdata    = rt_alloc_pdata,
    .free_pdata     = rt_free_pdata,
    .alloc_domdata  = rt_alloc_domdata,
    .free_domdata   = rt_free_domdata,
    .init_domain    = rt_dom_init,
    .destroy_domain = rt_dom_destroy,
    .alloc_vdata    = rt_alloc_vdata,
    .free_vdata     = rt_free_vdata,
    .insert_vcpu    = rt_vcpu_insert,
    .remove_vcpu    = rt_vcpu_remove,

    .adjust         = rt_dom_cntl,

    .pick_cpu       = rt_cpu_pick,
    .do_schedule    = rt_schedule,
    .sleep          = rt_vcpu_sleep,
    .wake           = rt_vcpu_wake,
    .context_saved  = rt_context_saved,
};

REGISTER_SCHEDULER(sched_rtds_def);
