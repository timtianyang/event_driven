#define XC_WANT_COMPAT_MAP_FOREIGN_API
#include <xenctrl.h>
#include <stdlib.h>
#include <xc_private.h>
#include "ezxml.h"
int main(int argc, char* argv[]){
    mode_change_info_t info = {
        .nr_old = 0,
        .nr_new = 0,
        .nr_changed = 0,
        .nr_unchanged = 0,
    };

    xen_domctl_schedparam_t *params;

    xc_interface *xci; 

    ezxml_t xml, new_v, old_v, changed_v, unchanged_v;
    int i = 0, nr_vcpus;
    int sync, domid, old_offset, new_offset, peri;

    if (argc != 2) return fprintf(stderr, "usage: %s xmlfile\n", argv[0]);

    xml = ezxml_parse_file(argv[1]);

    domid = atoi(ezxml_attr(xml,"domain"));
    sync = atoi(ezxml_attr(xml,"sync"));
    old_offset = atoi(ezxml_attr(xml,"old_offset"));
    new_offset = atoi(ezxml_attr(xml,"new_offset"));
    peri = atoi(ezxml_attr(xml,"peri"));

    printf("domain=%d sync=%d old_ofst=%d\n",domid, sync, old_offset);

    info.domid = domid;
    info.ofst_old = old_offset;
    info.ofst_new = new_offset;
    info.sync = sync;
    info.peri = peri;
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
    params = malloc(sizeof(xen_domctl_schedparam_t) * nr_vcpus);

    for(i = 0, new_v = ezxml_child(xml, "new_v"); new_v; new_v = new_v->next, i++)
    {
        printf("vcpu id = %s\n", ezxml_child(new_v, "id")->txt);
        printf("p = %s\n", ezxml_child(new_v, "period")->txt);
        printf("b = %s\n", ezxml_child(new_v, "budget")->txt);
        params[i].type = NEW;
        params[i].vcpuid = atoi(ezxml_child(new_v, "id")->txt);
        params[i].rtds.period = atoi(ezxml_child(new_v, "period")->txt);
        params[i].rtds.budget = atoi(ezxml_child(new_v, "budget")->txt); 
    }

    printf("\nold_vcpus:\n");
    for(old_v = ezxml_child(xml, "old_v"); old_v; old_v = old_v->next, i++)
    {
        printf("vcpu id = %s\n", ezxml_child(old_v, "id")->txt);
        params[i].type = OLD;
        params[i].vcpuid = atoi(ezxml_child(old_v, "id")->txt);
    }

    printf("\nchanged_vcpus:\n");
    for(changed_v = ezxml_child(xml, "changed_v"); changed_v; changed_v = changed_v->next, i++)
    {
        printf("vcpu id = %s\n", ezxml_child(changed_v, "id")->txt);
        printf("p = %s\n", ezxml_child(changed_v, "period")->txt);
        printf("b = %s\n", ezxml_child(changed_v, "budget")->txt);
        params[i].type = CHANGED;
        params[i].vcpuid = atoi(ezxml_child(changed_v, "id")->txt);
        params[i].rtds.period = atoi(ezxml_child(changed_v, "period")->txt);
        params[i].rtds.budget = atoi(ezxml_child(changed_v, "budget")->txt);
    }

    printf("\nunchanged_vcpus:\n");
    for(unchanged_v = ezxml_child(xml, "unchanged_v"); unchanged_v; unchanged_v = unchanged_v->next, i++)
    {
        printf("vcpu id = %s\n", ezxml_child(unchanged_v, "id")->txt);
        params[i].type = UNCHANGED;
        params[i].vcpuid = atoi(ezxml_child(unchanged_v, "id")->txt);
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
}
