#define XC_WANT_COMPAT_MAP_FOREIGN_API
#include <xenctrl.h>
#include <stdlib.h>
#include <xc_private.h>

int main(int argc, char* argv[])
{
    int mode_id;
    xc_interface *xci;
    mode_change_info_t info;
    int domid;

    if ( argc != 3 ) return fprintf(stderr, "usage: %s domid trans_id\n", argv[0]);

    mode_id = atoi(argv[2]);
    domid = atoi(argv[1]);

    if ( mode_id < 0 )
        return fprintf(stderr, "trans_id shouldn't be negative\n");

    info.mode_id = mode_id;
    printf("trans_id %d will be triggered\n", mode_id);

    xci = xc_interface_open(0, 0, 0);

    if ( !xci )
    {
        fprintf(stderr, "Failed to open an xc handler");
        goto out;
        return 1;
    }

//    printf("before going to xc\n");
    xc_sched_rtds_mc_trigger(xci, domid, info);

//    printf("after xc in main\n");
    
out:
    xc_interface_close(xci);

}
