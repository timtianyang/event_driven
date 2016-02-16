#define XC_WANT_COMPAT_MAP_FOREIGN_API
#include <xenctrl.h>
#include <stdlib.h>
#include <xc_private.h>
int main(void){
    xen_domctl_mc_proto_t protocol;
    uint32_t domid = 0;

    xc_interface *xci = xc_interface_open(NULL, NULL, 0);

    if ( !xci )
    {
        fprintf(stderr, "Failed to open an xc handler");
        return 1;
    }
    protocol.domain_id = 0;

    protocol.nr_old_vcpus = 2;
    protocol.old_vcpus = malloc(sizeof(uint16_t)*protocol.nr_old_vcpus);

    protocol.old_vcpus[0] = 3;
    protocol.old_vcpus[1] =2;

    protocol.nr_new_vcpus = 1;
    protocol.new_vcpus = malloc(sizeof(uint16_t)*protocol.nr_new_vcpus);

    protocol.new_vcpus[0] =0;

    protocol.nr_unchanged_vcpus = 1;
    protocol.unchanged_vcpus = malloc(sizeof(uint16_t)*protocol.nr_unchanged_vcpus);

    protocol.unchanged_vcpus[0] =1;

    protocol.nr_changed_vcpus = 0;

    protocol.ofst_old = 1000;
    protocol.ofst_new = 2000;

    protocol.peri = 1;
    protocol.sync = 1;

    xc_sched_rtds_mc_set(xci, domid, &protocol);

    printf("after xc in main\n");

    xc_interface_close(xci);
    printf("closed xc interface....\n");
    return 0;
}
