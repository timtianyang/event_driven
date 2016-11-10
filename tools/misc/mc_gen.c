#include <xenctrl.h>
#include <stdlib.h>
#include <xc_private.h>
#include <inttypes.h>
#include "ezxml.h"
#include <time.h>

void error(void){
    exit(1);
}

void write_mode0(int vcpu_num, FILE* fp){
    int i;
    long p = 10000000;
    long b = p / vcpu_num;

    fprintf(fp, "    <mode id=\"0\">\n");
    for ( i = 0; i < vcpu_num; i++ )
    {
        fprintf(fp, "        <vcpu id=\"%d\" budget=\"%ld\" period=\"%ld\" criticality=\"0\"/>\n", i, b, p);
    }
    fprintf(fp, "    </mode>\n");
}

/*
 * generate a test case. The max budget is 5ms
 * the period is always calculated using budget
 * to make sure it's schedulable
 */
void write_mode1(int vcpu_num, FILE* fp){
    int* vcpus;
    int num_vcpu_in_modes;
    int i, j, found = 0;
    //#define MAX_PERIOD 10000000UL /* 10ms */
    //#define MIN_BUDGET 1000000UL  /* 1ms */
    //unsigned long max_budget = MAX_PERIOD / vcpu_num;

    fprintf(fp, "    <mode id=\"1\">\n");
    num_vcpu_in_modes = rand() % vcpu_num + 1;
    vcpus = malloc(sizeof(int) * num_vcpu_in_modes);
    for ( i = 0; i < num_vcpu_in_modes; i++ )
    {
        int gen_vcpu_id = rand() % num_vcpu_in_modes;

        for ( j = 0; j < i; j++ )
        {
            if ( gen_vcpu_id == vcpus[j] )
            {
                found = 1;
                break;
            }
        }
        if ( found )
        {
            i--;
            found = 0;
        }
        else
            vcpus[i] = gen_vcpu_id;
    }

    /* done generating list of vcpus, now parameters */
    for ( i = 0; i < num_vcpu_in_modes; i++ )
    {
        long b = ( rand() % 400 + 100) * 10000;
        long p = b * vcpu_num;
        //printf("v %d b %ld p %ld", vcpus[i], b, p);
        fprintf(fp, "        <vcpu id=\"%d\" budget=\"%ld\" period=\"%ld\" criticality=\"0\"/>\n", vcpus[i], b, p);
    }
    //printf("\n");
    fprintf(fp, "    </mode>\n");
}
/*
 * arg1: vcpu_num
 * arg2: output path, created xmls will be appended with numbers
 * arg3: number of tests/transitions
 */
int main(int argc, char* argv[]){
    FILE *fp;
    char name[50];
    char id_str[10];
    int trans_id;
    int vcpu_num;
    int tests_num;

    if ( argc != 4 )
    {
        printf("usage: vcpu_num output_name");
        error();
    }

    srand(time(NULL));
    vcpu_num = atoi(argv[1]);
    tests_num = atoi(argv[3]);    
    printf("generating %d tests for %d vcpus\n", tests_num ,vcpu_num);

    for ( trans_id = 0; trans_id < tests_num; trans_id++ )
    {
        /* prepare file to write out */
        sprintf(id_str, "%d", trans_id);
        memcpy(name, argv[2], strlen(argv[2])+1);
        strcat(name, "sys");
        strcat(name,id_str);
        strcat(name,".xml");
        fp = fopen(name, "w");
        if ( fp == NULL )
        {
            printf("failed to create file %s\n", name);
            error();
        }

        fprintf(fp, "<sys cpu=\"3\" domain=\"2\">\n");
        
        

        write_mode0(vcpu_num, fp);
       
        write_mode1(vcpu_num, fp); 
        fprintf(fp, "    <trans id=\"0\" src=\"0\" dst=\"1\"/>\n");
        fprintf(fp, "</sys>");
        fclose(fp);
    }

    

}
