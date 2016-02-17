#define XC_WANT_COMPAT_MAP_FOREIGN_API
#include <xenctrl.h>
#include <stdlib.h>
#include <xc_private.h>
int main(void){
    xen_domctl_mc_proto_t protocol;
    uint32_t domid = 1;

    xc_interface *xci = xc_interface_open(NULL, NULL, 0);

    if ( !xci )
    {
        fprintf(stderr, "Failed to open an xc handler");
        return 1;
    }
    protocol.domain_id = 1;

    protocol.nr_old_vcpus = 1;
    protocol.old_vcpus = malloc(sizeof(uint16_t)*protocol.nr_old_vcpus);
    protocol.old_vcpus[0] = 3;
    //protocol.old_vcpus[1] =2;

    protocol.nr_new_vcpus = 0;
   // protocol.new_vcpus = malloc(sizeof(uint16_t)*protocol.nr_new_vcpus);
   // protocol.new_vcpus[0] =0;

    protocol.nr_changed_vcpus = 0;
   // protocol.changed_vcpus = malloc(sizeof(uint16_t)*protocol.nr_changed_vcpus);
   // protocol.changed_vcpus[0] =1;
   // protocol.changed_vcpus[1] =0;
   // protocol.new_params = malloc(sizeof(struct xen_domctl_sched_rtds)*protocol.nr_changed_vcpus);
   // protocol.new_params[0].period = 50000;
   // protocol.new_params[0].budget = 40000;
   // protocol.new_params[1].period = 70000;
   // protocol.new_params[1].budget = 60000;


    protocol.nr_unchanged_vcpus = 3;
    protocol.unchanged_vcpus = malloc(sizeof(uint16_t)*protocol.nr_unchanged_vcpus);
    protocol.unchanged_vcpus[0] =0;
    protocol.unchanged_vcpus[1] =1;
    protocol.unchanged_vcpus[2] =2;


    protocol.ofst_old = 0;
    protocol.ofst_new = 0;

    protocol.peri = 1;
    protocol.sync = 1;

    xc_sched_rtds_mc_set(xci, domid, &protocol);

    printf("after xc in main\n");

    xc_interface_close(xci);
    printf("closed xc interface....\n");
    return 0;
}
