#define XC_WANT_COMPAT_MAP_FOREIGN_API
#include <xenctrl.h>
#include <stdlib.h>
#include <xc_private.h>
#include "ezxml.h"

#define RTDS_BUDGET "4000000"
#define RTDS_PERIOD "10000000"
#define MC_ZERO "0"
#define MC_NO "0"
#define MC_YES "1"

//#define MC_DEBUG

void error(void){
    exit(1);
}

ezxml_t get_child(ezxml_t parent, const char* child_name)
{
    ezxml_t temp = ezxml_child(parent, child_name);
    if( temp == NULL )
    {
        printf("error fetching %s\n",child_name);
        error();
    }
    return temp;
}

ezxml_t try_get_child(ezxml_t parent, const char* child_name)
{
    ezxml_t temp = ezxml_child(parent, child_name);
    return temp;
}

const char* get_attr(ezxml_t parent, const char* attr_name)
{
    const char* temp = ezxml_attr(parent, attr_name);
    if( temp == NULL )
    {
        printf("error fetching %s\n", attr_name);
        error();
    }
    return temp;
}

/*
 * sets the comparison guard using a parent tag
 */
void set_guard_comp(xen_domctl_sched_guard_t* cur_guard, ezxml_t parent, int type)
{
    ezxml_t temp;
    const char* comp;

    switch (type)
    {
        case MC_BUDGET: 
            temp = get_child(parent, "budget");
            cur_guard->b_comp.budget_thr = atol(temp->txt);
            comp = get_attr(temp, "type");
            if(strcmp(comp, ">=") == 0)
                cur_guard->b_comp.comp = MC_LARGER_THAN;
            else if (strcmp(comp, "<=") == 0)
                cur_guard->b_comp.comp = MC_SMALLER_THAN;
/*            else if (strcmp(temp->txt, "=") == 0)
                cur_guard->b_comp.comp = MC_EQUAL_TO;*/
            else
            {
                printf("unknown comparator %s\n",temp->txt);
                error();
            }
            #ifdef MC_DEBUG
            printf("budget comp %s %ld\n", comp, cur_guard->b_comp.budget_thr);
            #endif
            break;
        case MC_BACKLOG:
            temp = get_child(parent, "size");
            cur_guard->buf_comp.len = atoi(temp->txt);
            comp = get_attr(temp, "type");
            if(strcmp(comp, ">=") == 0)
                cur_guard->buf_comp.comp = MC_LARGER_THAN;
            else if (strcmp(comp, "<=") == 0)
                cur_guard->buf_comp.comp = MC_SMALLER_THAN;
//            else if (strcmp(temp->txt, "=") == 0)
//                cur_guard->buf_comp.comp = MC_EQUAL_TO;
            else
            {
                printf("unknown comparator %s\n",temp->txt);
                error();
            }
            #ifdef MC_DEBUG
            printf("backlog comp %s %d\n", comp, cur_guard->buf_comp.len);
            #endif
            break;
    }            
}

int get_guard_type(const char* type)
{
    if(strcmp(type, "time") == 0)
        return MC_TIME_FROM_MCR;
    if (strcmp(type, "timer") == 0 )
        return MC_TIMER_FROM_LAST_RELEASE;
    if (strcmp(type, "budget") == 0)
        return MC_BUDGET;
    if (strcmp(type, "period") == 0)
        return MC_PERIOD;
    if (strcmp(type, "backlog") == 0)
        return MC_BACKLOG;
     if (strcmp(type, "none") == 0)
        return MC_NO_NEW_GUARD;   
    printf("unrecognized guard type %s\n", type);
    error();
    return -1;
}
/*
 * old_new: 0 ->old 1->new
 * parent must be a pointer to the guard tag
 */
void set_vcpu_guard(xen_domctl_schedparam_t* cur, int old_new, int type, ezxml_t parent)
{
    ezxml_t temp;
    xen_domctl_sched_guard_t* cur_guard;
    cur_guard = (old_new == 0) ? &cur->guard_old:
                                 &cur->guard_new;

    switch (type)
    {
        case MC_TIME_FROM_MCR:
            if(!old_new)
                cur->guard_old_type = MC_TIME_FROM_MCR;
            else
                cur->guard_new_type = MC_TIME_FROM_MCR;
            temp = get_child(parent, "time");
            cur_guard->t_from_MCR = atol(temp->txt);

            #ifdef MC_DEBUG
            printf("time from mcr %ld\n",cur_guard->t_from_MCR);
            #endif
            break;
        case MC_TIMER_FROM_LAST_RELEASE:
            if(!old_new)
                cur->guard_old_type = MC_TIMER_FROM_LAST_RELEASE;
            else
                cur->guard_new_type = MC_TIMER_FROM_LAST_RELEASE; 
            temp = get_child(parent, "timer");
            cur_guard->t_from_last_release = atol(temp->txt);

            #ifdef MC_DEBUG
            printf("timer from last %ld\n",cur_guard->t_from_last_release);
            #endif
            break;
        case MC_BUDGET:
            if(!old_new)
                cur->guard_old_type = MC_BUDGET;
            else
                cur->guard_new_type = MC_BUDGET;
            set_guard_comp(cur_guard, parent, MC_BUDGET);
            break;
        case MC_PERIOD:
            if(!old_new)
                cur->guard_old_type = MC_PERIOD;
            else
                cur->guard_new_type = MC_PERIOD;

            temp = get_child(parent, "old_smaller");
            if(strcmp(temp->txt, "old") == 0)
            {
                cur_guard->p_comp.action_old_small = MC_USE_OLD;
                #ifdef MC_DEBUG
                printf("period: action old smaller use old\n");
                #endif
            }
            else if (strcmp(temp->txt, "new") == 0)
            {
                cur_guard->p_comp.action_old_small = MC_USE_NEW;
                #ifdef MC_DEBUG
                printf("period: action old smaller use new\n");
                #endif
            }
            else
            {
                printf("unknown action %s\n",temp->txt);
                error();
            }
            temp = get_child(parent, "new_smaller");
            if(strcmp(temp->txt, "old") == 0)
            {
                cur_guard->p_comp.action_new_small = MC_USE_OLD;
                #ifdef MC_DEBUG
                printf("period: action new smaller use old\n");
                #endif
            }
            else if (strcmp(temp->txt, "new") == 0)
            {
                cur_guard->p_comp.action_new_small = MC_USE_NEW;
                #ifdef MC_DEBUG
                printf("period: action new smaller use new\n");
                #endif
            }
            else
            {
                printf("unknown action %s\n",temp->txt);
                error();
            }
            break;
        case MC_BACKLOG:
            if(!old_new)
                cur->guard_old_type = MC_BACKLOG;
            else
                cur->guard_new_type = MC_BACKLOG; 

            set_guard_comp(cur_guard, parent, MC_BACKLOG);
            break;
        case MC_NO_NEW_GUARD:
            cur->guard_new_type = MC_NO_NEW_GUARD;
            break;
        default:
            printf("unknow guard type: %d\n",type);
            error();
    }
}

/*
 * set a vcpu's action to disable old, dis is the disable_old tag
 */
void set_vcpu_old_action(xen_domctl_schedparam_t *cur, ezxml_t dis)
{
    ezxml_t tmp = get_child(dis, "action_running_old");
    if(strcmp(tmp->txt, "continue") == 0)
    {
        cur->action_running_old = MC_CONTINUE;
        #ifdef MC_DEBUG
        printf("action_running_old is continue\n");
        #endif
    }
    else if(strcmp(tmp->txt, "abort") == 0)
    {
        cur->action_running_old = MC_ABORT;
        #ifdef MC_DEBUG
        printf("action_running_old is abort\n");
        #endif
    }
    else
    {
        printf("unknow action for running_old\n");
        error();
    }

    tmp = get_child(dis, "action_not_running_old");
    if(strcmp(tmp->txt, "continue") == 0)
    {
        cur->action_not_running_old = MC_CONTINUE;
        #ifdef MC_DEBUG
        printf("action_not_running_old is continue\n");
        #endif
    }
    else if(strcmp(tmp->txt, "abort") == 0)
    {
        cur->action_not_running_old = MC_ABORT;
        #ifdef MC_DEBUG
        printf("action_not_running_old is abort\n");
        #endif
    }
    else if (strcmp(tmp->txt, "update") == 0)
    {
        long dbudget = atol(get_attr(tmp, "dbudget"));
        long ddeadline = atol(get_attr(tmp, "ddeadline"));
        cur->dbudget = dbudget;
        cur->ddeadline = ddeadline;
        cur->action_not_running_old = MC_UPDATE;
        #ifdef MC_DEBUG
        printf("delta budget is %ld\n",cur->dbudget);
        printf("delta deadline is %ld\n",cur->ddeadline);
        #endif
    }
    else if(strcmp(tmp->txt, "guard") == 0)
    {
        int old_guard_type;
        cur->action_not_running_old = MC_USE_GUARD;
        #ifdef MC_DEBUG
        printf("action_not_running_old is use guard\n");
        printf("guard_old:\n");
        #endif
        tmp = get_child(dis, "guard_old");
        old_guard_type = get_guard_type(get_attr(tmp, "type"));
        set_vcpu_guard(cur, 0, old_guard_type, tmp);
    }
    else
    {
        printf("unknow action for not_running_old\n");
        error();
    }
}

/*
 * set a vcpu's param for a new vcpu, v is the vcpu tag
 */
void set_vcpu_param(xen_domctl_schedparam_t *cur, ezxml_t v)
{
    ezxml_t tmp = get_child(v, "budget");
    cur->rtds.budget = atoi(tmp->txt);
    
    
    tmp = get_child(v, "period");
    cur->rtds.period = atoi(tmp->txt);
    tmp = get_child(v, "criticality");
    cur->rtds.crit = atoi(tmp->txt);
    #ifdef MC_DEBUG
    printf("b = %d ", cur->rtds.budget);
    printf("p = %d\n", cur->rtds.period);
    printf("crit = %d\n", cur->rtds.crit);
    #endif
}

int main(int argc, char* argv[]){
    static mode_change_info_t info;

    xen_domctl_schedparam_t *params;

    xc_interface *xci; 

    ezxml_t xml, v, tmp;
    int i = 0;
    int domid, cpu;
    const char* s; //tmp string

    if (argc != 2) return fprintf(stderr, "usage: %s xmlfile\n", argv[0]);

    xml = ezxml_parse_file(argv[1]);
    if ( xml == NULL )
    {
        printf("Failed to open the sys file\n");
        error();
    }

    s = ezxml_attr(xml,"domain");
    if(s == NULL)
    {
        printf("Domain id cannot be NULL\n");
        return 0;
    }
    
    domid = atoi(s);
    #ifdef MC_DEBUG
    printf("domain=%d\n",domid);
    #endif
    info.domid = domid;

    s = ezxml_attr(xml,"cpu");
    if(s == NULL)
    {
        printf("CPU id cannot be NULL\n");
        return 0;
    }
    cpu = atoi(s);
    #ifdef MC_DEBUG
    printf("cpu=%d\n",cpu);
    #endif
    info.cpu = cpu;

    for(v = ezxml_child(xml, "vcpu"); v; v = v->next)
    {
        info.nr_vcpus++; 
    }

    #ifdef MC_DEBUG
    printf("%d of vcpus specified in this file\n", info.nr_vcpus);
    #endif
    /* done calculating size */
    params = malloc((sizeof(xen_domctl_schedparam_t)) * info.nr_vcpus);

    /* iterating through all vcpus */
    for(i = 0, v = ezxml_child(xml, "vcpu"); v; v = v->next, i++)
    {
        int new_guard_type;
        //int old_guard_type;
        xen_domctl_schedparam_t *cur = params + i;

        s = get_attr(v, "id");        
        cur->vcpuid = atoi(s);
        #ifdef MC_DEBUG
        printf("vcpu %s is ", s);
        #endif
        s = get_attr(v, "type");
        #ifdef MC_DEBUG
        printf("%s\n",s);
        #endif

        if( strcmp(s,"new") == 0 )
            cur->type = NEW;
        else if( strcmp(s,"old") == 0 )
            cur->type = OLD;
        else if( strcmp(s,"changed") == 0 )
            cur->type = CHANGED;
        else if( strcmp(s,"unchanged") == 0 )
            cur->type = UNCHANGED;

        switch(cur->type)
        {
            case NEW:
                tmp = get_child(v, "release_new");
                tmp = get_child(tmp, "guard_new");
                new_guard_type = get_guard_type(get_attr(tmp, "type"));
                #ifdef MC_DEBUG
                printf("guard_new:\n");
                #endif
                set_vcpu_guard(cur, 1, new_guard_type, tmp);
                set_vcpu_param(cur,v);
                break;
            case OLD:
                tmp = get_child(v, "disable_old");
                set_vcpu_old_action(cur,tmp);
                break;
            case CHANGED: /* fall through */
                set_vcpu_param(cur,v);
            case UNCHANGED:
                tmp = get_child(v, "disable_old");
                set_vcpu_old_action(cur,tmp);
                tmp = get_child(v, "release_new");
                tmp = get_child(tmp, "guard_new");
                #ifdef MC_DEBUG
                printf("guard_new:\n");
                #endif
                new_guard_type = get_guard_type(get_attr(tmp, "type"));
                set_vcpu_guard(cur, 1, new_guard_type, tmp);
                break;
        }
        #ifdef MC_DEBUG
        printf("----\n");
        #endif
    }
    #ifdef MC_DEBUG
    printf("+++++end of mc parsing+++++\n");
    #endif
/*
    for (i=0; i<info.nr_vcpus; i++)
    {
        printf("-------\n");
        printf("param[%d].vcpuid = %d\n", i, params[i].vcpuid);
        printf("type = %d\n", params[i].type);
        printf("action_running_old = %d\n", params[i].action_running_old);
        printf("action_not_running_old = %d\n", params[i].action_not_running_old);
        printf("guard_old_type = %d\n", params[i].guard_old_type);
        printf("guard_new_type = %d\n", params[i].guard_new_type);
        printf("guard_o.t_from_MCR = %ld\n", params[i].guard_old.t_from_MCR);
        printf("guard_o.t_from_last = %ld\n", params[i].guard_old.t_from_last_release);
        printf("guard_o.budget_thr = %ld\n", params[i].guard_old.b_comp.budget_thr);
        printf("guard_o.comp = %d\n", params[i].guard_old.b_comp.comp);
        printf("guard_o.p_comp.action_old = %d\n", params[i].guard_old.p_comp.action_old_small);
        printf("guard_o.p_comp.action_new = %d\n", params[i].guard_old.p_comp.action_new_small);
        printf("guard_o.buf.len = %d\n", params[i].guard_old.buf_comp.len);
        printf("guard_o.buf.comp = %d\n", params[i].guard_old.buf_comp.comp);
        printf("----\n");
        printf("guard_n.t_from_MCR = %ld\n", params[i].guard_new.t_from_MCR);
        printf("guard_n.t_from_last = %ld\n", params[i].guard_new.t_from_last_release);
        printf("guard_n.budget_thr = %ld\n", params[i].guard_new.b_comp.budget_thr);
        printf("guard_n.comp = %d\n", params[i].guard_new.b_comp.comp);
        printf("guard_n.p_comp.action_old = %d\n", params[i].guard_new.p_comp.action_old_small);
        printf("guard_n.p_comp.action_new = %d\n", params[i].guard_new.p_comp.action_new_small);
        printf("guard_n.buf.len = %d\n", params[i].guard_new.buf_comp.len);
        printf("guard_n.buf.comp = %d\n", params[i].guard_new.buf_comp.comp);
 
    }
*/

    i = fprintf(stderr, "%s\n", ezxml_error(xml));
    ezxml_free(xml);

//    printf("opening interface...\n");
    xci = xc_interface_open(0, 0, 0);

    if ( !xci )
    {
        fprintf(stderr, "Failed to open an xc handler");
        goto out;
        return 1;
    }

//    printf("before going to xc\n");
    xc_sched_rtds_mc_set(xci, domid, info, params);

//    printf("after xc in main\n");
    
out:
    xc_interface_close(xci);
//    printf("closed xc interface....\n");
    return 0;
}
