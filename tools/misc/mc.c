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
    protocol.temp = 3;

    
    xc_sched_rtds_mc_set(xci, domid, &protocol);


protocol.temp = 6;

    
    xc_sched_rtds_mc_set(xci, domid, &protocol);


protocol.temp = 7;

    
    xc_sched_rtds_mc_set(xci, domid, &protocol);

    printf("after xc in main\n");

    xc_interface_close(xci);
    printf("closed xc interface....\n");
    return 0;
}
