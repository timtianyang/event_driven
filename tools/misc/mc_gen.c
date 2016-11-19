#include <xenctrl.h>
#include <stdlib.h>
#include <xc_private.h>
#include <inttypes.h>
#include "ezxml.h"
#include <time.h>

void error(void){
    exit(1);
}

/*
 * Write the initial mode to fp. It always
 * assumes all vcpus are enabled and schedulable.
 */
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
 * Write a random mode to fp. The max budget is 5ms
 * the period is always calculated using budget
 * to make sure it's schedulable.
 */
void write_mode(int vcpu_num, int mode_id, FILE* fp){
    int* vcpus;
    int num_vcpu_in_modes;
    int i, j, found = 0;

    fprintf(fp, "    <mode id=\"%d\">\n", mode_id);
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
 * Write transistions to fp. The transistions will be generated
 * in order. 1->2, 1->3, 2->3, and reverse  3->2, 3->1, 2->1
 * if num of trans is smaller than all combinations, then stop
 */
void write_trans(int num_modes, int max_num_trans, FILE* fp){
    int i, j;
    int trans_id = 0;

    for ( i = 0; i < num_modes - 1; i++ )
    {
        for ( j = i + 1; j < num_modes; j++ )
        {
            fprintf(fp, "    <trans id=\"%d\" src=\"%d\" dst=\"%d\"/>\n", trans_id++, i, j);
            if ( trans_id == max_num_trans )
                return;
        }
    }
    for ( i = num_modes -1; i >= 1; i-- )
    {
        for ( j = i - 1; j >= 0; j-- )
        {
            fprintf(fp, "    <trans id=\"%d\" src=\"%d\" dst=\"%d\"/>\n", trans_id++, i, j);
            if ( trans_id == max_num_trans )
                return;
        }
    }

}

/*
 * arg1: vcpu_num
 * arg2: output path, created xmls will be appended with numbers
 * arg3: number of tests
 * arg4: number of modes
 * arg5: number of random transitions. 
 */
int main(int argc, char* argv[]){
    FILE *fp;
    char name[50];
    char id_str[10];
    int test_id;
    int mode_id;

    int vcpu_num;   /* max num of vcpus in the system */
    int tests_num;  /* num of tests to be generated */
    int mode_num;   /* num of modes to be generated in a test */
    int trans_num;  /* num of transistions to be generated */

    if ( argc != 6 )
    {
        printf("usage: vcpu_num output_name num_of_tests num_of_modes num_of_transitions\n");
        error();
    }

    srand(time(NULL));
    vcpu_num = atoi(argv[1]);
    tests_num = atoi(argv[3]);
    mode_num = atoi(argv[4]);
    trans_num = atoi(argv[5]);

    if ( !tests_num || !mode_num || mode_num == 1 || !trans_num )
    {
        printf("error num of modes, tests or transitions.\n");
        printf("number of modes must be larger than 1 and transistions and tests must not be zero\n");
        error();
    }

    printf("generating %d tests for %d vcpus\n", tests_num ,vcpu_num);
    printf("%d of modes, %d of transistions\n", mode_num, trans_num);

    for ( test_id = 0; test_id < tests_num; test_id++ )
    {
        /* prepare file to write out */
        sprintf(id_str, "%d", test_id);
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


        /* mode0 always has all vcpus enabled */
        write_mode0(vcpu_num, fp);

        for ( mode_id = 1; mode_id < mode_num; mode_id++ )
            write_mode(vcpu_num, mode_id, fp);

        write_trans(mode_num, trans_num, fp);
        fprintf(fp, "</sys>");
        fclose(fp);
    }
}
