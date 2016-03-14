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
/*
 * Privimitives:
 * offset_new: Time between MCR and releasing vcpus in the new mode.
 *  + default = 0
 * offset_old: Time between MCR and disabling vcpus in the old mode.
 *  + default = 0

 * disable_running: Can the vcpu be disabled while running?
 *  + default = False
 * disable_released: Can the vcpu be disabled while on run queue?
 *  + default = False
 * disable_released: Can the vcpu be disabled while not runnable?
 *  + default = True

 */
int main(int argc, char* argv[]){
    static mode_change_info_t info;

    xen_domctl_schedparam_t *params;

    xc_interface *xci; 

    ezxml_t xml, new_v, old_v, changed_v, unchanged_v, tmp;
    int i = 0, nr_vcpus;
    int domid;
    const char* s; //tmp string

    if (argc != 2) return fprintf(stderr, "usage: %s xmlfile\n", argv[0]);

    xml = ezxml_parse_file(argv[1]);

    s = ezxml_attr(xml,"domain");
    if(s == NULL)
    {
        printf("Domain id cannot be NULL\n");
        return 0;
    }
    domid = atoi(s);

/*
    s = ezxml_attr(xml,"sync");

    if(s != NULL)
        sync = atoi(s);

    s = ezxml_attr(xml,"peri");

    if(s != NULL)
        peri = atoi(s);
*/
    printf("domain=%d\n",domid);

    info.domid = domid;
    //info.ofst_old = old_offset;
    //info.ofst_new = new_offset;
    info.guard_old = 0;

    printf("new vcpus:\n");
    for(new_v = ezxml_child(xml, "new_v"); new_v; new_v = new_v->next)
    {
        info.nr_new++; 
    }
    for(old_v = ezxml_child(xml, "old_v"); old_v; old_v = old_v->next)
    {
        info.nr_old++;
    }
    for(changed_v = ezxml_child(xml, "changed_v"); changed_v; changed_v = changed_v->next)
    {
        info.nr_changed++;
    }
    for(unchanged_v = ezxml_child(xml, "unchanged_v"); unchanged_v; unchanged_v = unchanged_v->next)
    {
        info.nr_unchanged++;
    }

    nr_vcpus = info.nr_new + info.nr_old + info.nr_changed + info.nr_unchanged;
    
    /* done calculating size */
    params = malloc(sizeof(xen_domctl_schedparam_t) * nr_vcpus);

    for(i = 0, new_v = ezxml_child(xml, "new_v"); new_v; new_v = new_v->next, i++)
    {
        
        params[i].type = NEW;
        
        tmp = ezxml_child(new_v, "id");
        
        if(tmp == NULL)
        {
            printf("id cannot be null\n");
            goto error;
        }
        s = tmp->txt;
        params[i].vcpuid = atoi(s);
        printf("vcpu id = %s\n", s);
        params[i].rtds.period = atoi(s =( (tmp = ezxml_child(new_v, "period")) == NULL ? RTDS_PERIOD: tmp->txt ));
        printf("p = %s\n", s);
        params[i].rtds.budget = atoi(s =( (tmp = ezxml_child(new_v, "budget")) == NULL ? RTDS_BUDGET: tmp->txt) );
        printf("b =i %s\n", s);
        params[i].ofst_new = atoi(s =( (tmp = ezxml_child(new_v, "offset_new")) == NULL ? MC_ZERO: tmp->txt) );
        printf("o_n = %s\n", s);
        params[i].ofst_old = atoi(s =( (tmp = ezxml_child(new_v, "offset_old")) == NULL ? MC_ZERO: tmp->txt) );
        printf("o_o = %s\n", s);
        
        params[i].disable_running = atoi(s =( (tmp = ezxml_child(new_v, "disable_running")) == NULL ? MC_NO: MC_YES) );
        printf("d_r = %s\n",s);
        params[i].disable_released = atoi(s =( (tmp = ezxml_child(new_v, "disable_released")) == NULL ? MC_NO: MC_YES) );
        printf("d_l = %s\n",s);
        params[i].disable_not_released = atoi(s =( (tmp = ezxml_child(new_v, "disable_not_released")) == NULL ? MC_YES: MC_NO) );
        printf("d_n = %s\n",s);
    }

    printf("\nold_vcpus:\n");
    for(old_v = ezxml_child(xml, "old_v"); old_v; old_v = old_v->next, i++)
    {
        tmp = ezxml_child(old_v, "id");
        if(tmp == NULL)
        {
            printf("id cannot be null\n");
            goto error;
        }
        s = tmp->txt;
        params[i].type = OLD;
        params[i].vcpuid = atoi(s);
        printf("vcpu id = %s\n", s);
        params[i].ofst_new = atoi(s =( (tmp = ezxml_child(old_v, "offset_new")) == NULL ? MC_ZERO: tmp->txt) );
        printf("o_n = %s\n", s);
        params[i].ofst_old = atoi(s =( (tmp = ezxml_child(old_v, "offset_old")) == NULL ? MC_ZERO: tmp->txt) );
        printf("o_o = %s\n", s);

        params[i].disable_running = atoi(s =( (tmp = ezxml_child(old_v, "disable_running")) == NULL ? MC_NO: MC_YES) );
        printf("d_r = %s\n",s);
        params[i].disable_released = atoi(s =( (tmp = ezxml_child(old_v, "disable_released")) == NULL ? MC_NO: MC_YES) );
        printf("d_l = %s\n",s);
        params[i].disable_not_released = atoi(s =( (tmp = ezxml_child(old_v, "disable_not_released")) == NULL ? MC_YES: MC_NO) );
        printf("d_n = %s\n",s);
    }

    printf("\nchanged_vcpus:\n");
    for(changed_v = ezxml_child(xml, "changed_v"); changed_v; changed_v = changed_v->next, i++)
    {
        params[i].type = CHANGED;
        tmp = ezxml_child(changed_v, "id");
        if(tmp == NULL)
        {
            printf("id cannot be null\n");
            goto error;
        }
        s = tmp->txt;
        params[i].vcpuid = atoi(s);
        printf("vcpi id = %s\n", s);
        params[i].rtds.period = atoi(s =( (tmp = ezxml_child(changed_v, "period")) == NULL ? RTDS_PERIOD: tmp->txt) );
        printf("p = %s\n", s);
        params[i].rtds.budget = atoi(s =( (tmp = ezxml_child(changed_v, "budget")) == NULL ? RTDS_BUDGET: tmp->txt) );
        printf("b = %s\n", s);
        params[i].ofst_new = atoi(s =( (tmp = ezxml_child(changed_v, "offset_new")) == NULL ? MC_ZERO: tmp->txt) );
        printf("o_n = %s\n", s);
        params[i].ofst_old = atoi(s =( (tmp = ezxml_child(changed_v, "offset_old")) == NULL ? MC_ZERO: tmp->txt) );
        printf("o_o = %s\n", s);
        
        params[i].disable_running = atoi(s =( (tmp = ezxml_child(changed_v, "disable_running")) == NULL ? MC_NO: MC_YES) );
        printf("d_r = %s\n",s);
        params[i].disable_released = atoi(s =( (tmp = ezxml_child(changed_v, "disable_released")) == NULL ? MC_NO: MC_YES) );
        printf("d_l = %s\n",s);
        params[i].disable_not_released = atoi(s =( (tmp = ezxml_child(changed_v, "disable_not_released")) == NULL ? MC_YES: MC_NO) );
        printf("d_n = %s\n",s);
    }

    printf("\nunchanged_vcpus:\n");
    for(unchanged_v = ezxml_child(xml, "unchanged_v"); unchanged_v; unchanged_v = unchanged_v->next, i++)
    {
        params[i].type = UNCHANGED;
        tmp = ezxml_child(unchanged_v, "id");
        if(tmp == NULL)
        {
            printf("id cannot be null\n");
            goto error;
        }
        s = tmp->txt;
        params[i].vcpuid = atoi(s);
        printf("vcpu id = %s\n", s);
        params[i].ofst_new = atoi(s =( (tmp = ezxml_child(unchanged_v, "offset_new")) == NULL ? MC_ZERO: tmp->txt) );
        printf("o_n = %s\n", s);
        params[i].ofst_old = atoi(s =( (tmp = ezxml_child(unchanged_v, "offset_old")) == NULL ? MC_ZERO: tmp->txt) );
        printf("o_o = %s\n", s);

        params[i].disable_running = atoi(s =( (tmp = ezxml_child(unchanged_v, "disable_running")) == NULL ? MC_NO: MC_YES));
        printf("d_r = %s\n",s);
        params[i].disable_released = atoi(s =( (tmp = ezxml_child(unchanged_v, "disable_released")) == NULL ? MC_NO: MC_YES));
        printf("d_l = %s\n",s);
        params[i].disable_not_released = atoi(s =( (tmp = ezxml_child(unchanged_v, "disable_not_released")) == NULL ? MC_YES: MC_NO));
        printf("d_n = %s\n",s);
    }


    i = fprintf(stderr, "%s", ezxml_error(xml));
    ezxml_free(xml);


    xci = xc_interface_open(NULL, NULL, 0);

    if ( !xci )
    {
        fprintf(stderr, "Failed to open an xc handler");
        goto out;
        return 1;
    }

    xc_sched_rtds_mc_set(xci, domid, info, params);

    printf("after xc in main\n");
    
out:
    xc_interface_close(xci);
    printf("closed xc interface....\n");
    return 0;
error:
    
    return 0;
}
