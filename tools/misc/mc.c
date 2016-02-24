#define XC_WANT_COMPAT_MAP_FOREIGN_API
#include <xenctrl.h>
#include <stdlib.h>
#include <xc_private.h>
int main(int argc, char* argv[]){
    xen_domctl_mc_proto_t protocol = {
        .nr_old_vcpus = 0,
        .nr_new_vcpus = 0,
        .nr_changed_vcpus = 0,
        .nr_unchanged_vcpus = 0,
    };
    uint64_t *current_list;
    struct xen_domctl_sched_rtds* current_param;
    uint32_t domid;
    char* filename;
    FILE* fp;
    char* line;
    size_t len = 0;
    int flag = 0;

    xc_interface *xci = xc_interface_open(NULL, NULL, 0);

    if ( !xci )
    {
        fprintf(stderr, "Failed to open an xc handler");
        return 1;
    }


    if( argc != 2)
    {
        printf("need mode change file\n");
    }
    else
    {
        filename = argv[1];
        fp = fopen(filename,"r");
        if( fp == NULL )
        {
            printf("fail to open file %s\n",filename);
            return 1;
        }

        while( getline(&line, &len, fp) != -1 )
        {
            char *p;
            /* gets rid off the linefeed */
            line[strlen(line)-1] = '\0';

            if( strcmp(line,"domain") == 0 )
            {
                flag = 0; /* domain info */
                printf("domain info:\n");
                continue;
            }
            else if( strcmp(line,"old") == 0 )
            {
                flag = 1; /* domain info */
                printf("old vcpus:\n");
                continue;
            }
            else if( strcmp(line,"new") == 0 )
            {
                flag = 3; /* domain info */
                printf("new vcpus:\n");
                continue;
            }
            else if( strcmp(line,"unchanged") == 0 )
            {
                flag = 2; /* domain info */
                printf("unchanged vcpus:\n");
                continue;
            }
            else if( strcmp(line,"changed") == 0 )
            {
                flag = 4; /* domain info */
                printf("changed vcpus:\n");
                continue;
            }

            if(flag == 0)
            {
                domid = atoi(line);
                printf("domain id = %d\n",domid);
                protocol.domain_id = domid;
            }
            else
            {
                int count = 0;
                int i = 0;
                int i_param = 0;
                char* copy_line = malloc(strlen(line)+1);
                memcpy(copy_line,line,strlen(line)+1);

                /* parse the line just to get a count */
                for(p = strtok(copy_line," "); p != NULL; p = strtok(NULL, " "))
                {
                    count++;
                }
                free(copy_line);
                printf("there are %d tokens\n",count);
                if( count%3 != 0 && flag >=3 )
                {
                    printf("failed: number of budget and period do not match!\n");
                    fclose(fp);
                    goto out;
                }
                switch(flag)
                {
                    case 1:
                    protocol.nr_old_vcpus = count;
                    protocol.old_vcpus = malloc(sizeof(uint16_t)*protocol.nr_old_vcpus);
                    current_list = protocol.old_vcpus;

                    break;
                    case 2:
                    protocol.nr_unchanged_vcpus = count;
                    protocol.unchanged_vcpus = malloc(sizeof(uint16_t)*protocol.nr_unchanged_vcpus);
                    current_list = protocol.unchanged_vcpus;

                    break;
                    case 3:
                    protocol.nr_new_vcpus = count/3;
                    protocol.new_vcpus = malloc(sizeof(uint16_t)*protocol.nr_new_vcpus);
                    protocol.new_params = malloc(sizeof(struct xen_domctl_sched_rtds)*protocol.nr_new_vcpus);
                    current_list = protocol.new_vcpus;
                    current_param = protocol.new_params;
                    break;
                    case 4:
                    protocol.nr_changed_vcpus = count/3;
                    protocol.changed_vcpus = malloc(sizeof(uint16_t)*protocol.nr_changed_vcpus);
                    protocol.changed_params = malloc(sizeof(struct xen_domctl_sched_rtds)*protocol.nr_changed_vcpus);
                    current_list = protocol.changed_vcpus;
                    current_param = protocol.changed_params;
                    break;
                }

                printf("allocating is done\n");

                /* parsing one line */
                for(p = strtok(line," "); p != NULL; p = strtok(NULL, " "))
                {
                    if( flag >= 3)
                    {
                        switch(i%3)
                        {
                            case 0: 
                                printf(" i_param=%d  vcpu%d ", i_param, atoi(p));
                                current_list[i_param] = atoi(p);
                                break;
                            case 1: 
                                printf(" i_param=%d  b=%d ",i_param, atoi(p));
                                current_param[i_param].period = 1000;//atoi(p);
                                break;
                            case 2: 
                                printf(" i_param=%d  p=%d ",i_param, atoi(p));
                                current_param[i_param++].budget = 2000;//atoi(p);
                                /* i_param has been incremented */
                                break;
                        }
                    }
                    else
                    {
                        printf("%d ", atoi(p));
                        current_list[i] = atoi(p);
                    }
                    i++;
                }
                printf("\n");
            }
        }
    printf("successfully formed a mode change request...\n");

    fclose(fp);

   // protocol.new_params = malloc(sizeof(struct xen_domctl_sched_rtds)*protocol.nr_changed_vcpus);
   // protocol.new_params[0].period = 50000;
   // protocol.new_params[0].budget = 40000;
   // protocol.new_params[1].period = 70000;
   // protocol.new_params[1].budget = 60000;
    protocol.ofst_old = 0;
    protocol.ofst_new = 0;

    protocol.peri = 1;
    protocol.sync = 1;

    xc_sched_rtds_mc_set(xci, domid, &protocol);

    printf("after xc in main\n");
    
    
    }
out:
    xc_interface_close(xci);
    printf("closed xc interface....\n");
    return 0;
}
