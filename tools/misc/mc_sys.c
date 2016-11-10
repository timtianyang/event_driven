#define XC_WANT_COMPAT_MAP_FOREIGN_API
#include <xenctrl.h>
#include <stdlib.h>
#include <xc_private.h>
#include <inttypes.h>
#include "ezxml.h"

struct vcpu{
    int id;
    int64_t budget;
    int64_t period;
    int crit;
    int type; 
};

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
char* get_vcpu_type(int src, int dst, int vcpu_id)
{

}
*/
/*
 * create two sorted(id) list of vcpus based on modes
 * and then return a new list of vcpus that has type fields
 * root tag is the parent of mode tags
 */
struct vcpu* get_vcpus_from_transition(ezxml_t root, int src, int dst, int* num)
{
    int num_v_src = 0, num_v_dst = 0, num_v_total = 0;//, num_v_dst = 0;
    int i = 0, j = 0, pos = 0;
    ezxml_t src_mode = NULL, dst_mode = NULL, temp, v;
    struct vcpu *src_v, *dst_v, *ret_v;
    /* find the src mode and dst mode first */
    //printf("src = mode%d dst = mode%d\n",src, dst);
    for( temp = ezxml_child(root, "mode"); temp; temp = temp->next )
    {
        if ( atoi(get_attr(temp, "id")) == src )
            src_mode = temp;
        else if ( atoi(get_attr(temp, "id")) == dst )
            dst_mode = temp;
    }
    if ( src_mode == NULL || dst_mode == NULL )
    {
        printf("failed to find src or dst mode in the system\n");
        error();
    }
    /* construct src mode vcpu list */
    for( v = ezxml_child(src_mode, "vcpu"); v; v = v->next )
        num_v_src++;
    //printf("src mode has %d vcpus\n", num_v_src);
 
    src_v = malloc(sizeof(struct vcpu) * num_v_src);
    /* get an un-sorted list first */
    for( v = ezxml_child(src_mode, "vcpu"); v; v = v->next )
    { 
        struct vcpu* cur_v = &src_v[i++];
        cur_v->id = atoi(get_attr(v, "id"));
        cur_v->budget = atol(get_attr(v, "budget"));
        cur_v->period = atol(get_attr(v, "period"));
        cur_v->crit = atol(get_attr(v, "criticality"));
    }

// bubble sort 
    for ( i = 0; i < num_v_src; i++ )
    {
        for ( j = i + 1; j < num_v_src; j++ )
        {
            if ( src_v[j].id < src_v[i].id )
            {//swap them 
                
                struct vcpu t = src_v[j];
                //printf("swap [j]=%d [i]=%d\n",src_v[j].id,src_v[i].id);
                src_v[j] = src_v[i];
                src_v[i] = t;
                
            }
        }
    }
    /*
    printf("sorted src mode vcpus:\n");
    for ( i = 0; i < num_v_src; i++)
    {
        printf("vcpu%d ",src_v[i].id);
    }
    printf("\n");
    */
/***************************************************/
    /* construct dst mode vcpu list */
    for( v = ezxml_child(dst_mode, "vcpu"); v; v = v->next )
        num_v_dst++;
    //printf("dst mode has %d vcpus\n", num_v_dst);
 
    dst_v = malloc(sizeof(struct vcpu) * num_v_dst);
    /* get an un-sorted list first */
    i = 0;
    for( v = ezxml_child(dst_mode, "vcpu"); v; v = v->next )
    { 
        struct vcpu* cur_v = &dst_v[i++];
        cur_v->id = atoi(get_attr(v, "id"));
        cur_v->budget = atol(get_attr(v, "budget"));
        cur_v->period = atol(get_attr(v, "period"));
        cur_v->crit = atol(get_attr(v, "criticality"));
        //printf("%d ", cur_v->id);
    }

    // bubble sort
    for ( i = 0; i < num_v_dst; i++ )
    {
        for ( j = i + 1; j < num_v_dst; j++ )
        {
            if ( dst_v[j].id < dst_v[i].id )
            { // swap them
                
                struct vcpu t = dst_v[j];
                //printf("swap [j]=%d [i]=%d\n", dst_v[j].id, dst_v[i].id);
                dst_v[j] = dst_v[i];
                dst_v[i] = t;
                
            }
        }
    }
    /*
    printf("sorted dst mode vcpus:\n");
    for ( i = 0; i < num_v_dst; i++)
    {
        printf("vcpu%d ",dst_v[i].id);
    }
    printf("\n");
    */
//get he size of total interesting vcpus
    for ( i = 0; i < num_v_src; i++ )
    {
        for ( j=0; j < num_v_dst; j++)
        {
            if ( src_v[i].id == dst_v[j].id )
            {
                num_v_total++;
               // printf("found %d\n",src_v[i].id);
                break;
            }
        }
        if ( j == num_v_dst )
        {
            num_v_total++;
            //printf("not found %d\n",src_v[i].id);
        }
    }

    for ( i = 0; i < num_v_dst; i++ )
    {
        //printf("looking for %d\n",dst_v[i]);
        for ( j=0; j < num_v_src; j++)
        {
            if ( src_v[j].id == dst_v[i].id )
                break;
        }
        if ( j == num_v_src )
        {
            num_v_total++;
            //printf("not found %d\n",dst_v[i].id);
        }
    }
    //printf("total = %d\n",num_v_total);
//fill in the actual vcpu list that are relevant to mode transition
    ret_v = malloc(sizeof(struct vcpu) * num_v_total);
    pos = 0;
    for ( i = 0; i < num_v_src; i++ )
    {
        for ( j=0; j < num_v_dst; j++)
        {
            if ( src_v[i].id == dst_v[j].id )
            {
                ret_v[pos] = src_v[i];
                if ( src_v[i].period == dst_v[j].period &&
                     src_v[i].budget == dst_v[j].budget )
                    ret_v[pos++].type = UNCHANGED;
                else
                {
                    ret_v[pos].period = dst_v[j].period;
                    ret_v[pos].budget = dst_v[j].budget;
                    ret_v[pos++].type = CHANGED;
                    
                }
                break;
            }
        }
        if ( j == num_v_dst )
        {
            ret_v[pos] = src_v[i];
            ret_v[pos++].type = OLD;
            //printf("not found %d\n",src_v[i].id);
        }
    }

    for ( i = 0; i < num_v_dst; i++ )
    {
        //printf("looking for %d\n",dst_v[i]);
        for ( j=0; j < num_v_src; j++)
        {
            if ( src_v[j].id == dst_v[i].id )
                break;
        }
        if ( j == num_v_src )
        {
            ret_v[pos] = dst_v[i];
            ret_v[pos++].type = NEW;
           // printf("not found %d\n",dst_v[i].id);
        }
    }
    //printf("total = %d\n",num_v_total);
    /*for ( i = 0; i < num_v_total; i++ )
    {
        printf("vcpu%d is ", ret_v[i].id);
        switch ( ret_v[i].type)
        {
            case NEW:
                printf("new\n");
                break;
            case OLD:
                printf("old\n");
                break;
            case UNCHANGED:
                printf("unchanged\n");
                break;
            case CHANGED:
                printf("changed\n");
                break;
        }
        
    }
    */
    *num = num_v_total;
    return ret_v;
}

/*
 * extracts the contexts inside of vcpu tags for all types
 * and set the pointers
 */
void extract_type_transition_text(const char* path, char** unchanged,
                                  char** changed, char** old, char** new)
{
    FILE * fp;
    char* line = NULL;
    size_t len = 0;
    int rules_found = 0;
    int i;
    int next_type = 1;
    char* types[4];

    fp = fopen(path, "r");
    if (fp == NULL)
    {
        printf("failed to open xml file\n");
        error();
    }

    while (getline(&line, &len, fp) != -1) {
        line[strlen(line) - 1] = '\0'; //remove linefeed
        //printf("%s", line);
        if (strcmp(line, "<rules>") == 0)
        {
            rules_found = 1;
            //printf("found rules\n");
            break;
        }
    }

    if (!rules_found)
    {
        printf("xml file should specify rules.\n");
        error();
    }

    for ( i = 0; i<4; i++)
        types[i] = malloc(sizeof(char) * 1024);

    for ( i = 0; i<4 ; i++)
    {
            int pos = 0;
            int next_type_index = 0;
            while ( next_type || strcmp(line, "    </vcpu>") != 0)
            {
                if (getline(&line, &len, fp) != -1)
                {
                    char* sub;
                    //printf("origin %s", line);
                    sub = strstr(line, "vcpu type=\"");
                    if ( sub != NULL )
                    {
                        sub += 11;
                        if ( strncmp(sub,"changed", 7) == 0)
                            next_type_index = 0;
                        else if ( strncmp(sub,"unchanged", 9) == 0)
                            next_type_index = 1;
                        else if ( strncmp(sub,"new", 3) == 0)
                            next_type_index = 2;
                        else if ( strncmp(sub,"old", 3) == 0)
                            next_type_index = 3;
                    }
                    if (next_type)
                        next_type = 0;
                    else if ( strcmp(line, "    </vcpu>\n") != 0 )
                    {
                        strcpy(types[next_type_index] + pos,line);
                        pos += strlen(line);
                    }

                    line[strlen(line) - 1] = '\0'; //remove linefeed
                }
                else
                {
                    printf("xml file should specify rules for all types.\n");
                    error();
                }
            }
            next_type = 1;
            //printf("next type\n");
    }
    *changed = types[0];
    *unchanged = types[1];
    *new = types[2];
    *old = types[3];
    /*printf("extracted changed:\n%s", *changed);
    printf("extracted unchanged:\n%s", *unchanged);
    printf("extracted new:\n%s", *new);
    printf("extracted old:\n%s", *old);*/
    fclose(fp);
}

char* get_vcpu_type(int type)
{
    switch (type)
    {
        case NEW:
            return "new";
        case OLD:
            return "old";
        case CHANGED:
            return "changed";
        case UNCHANGED:
            return "unchanged";
        default:
            return "unknown";
    }
}

/*
 * arg1: sys_xml
 * arg2: output path, created xmls will be appended with numbers
 */
int main(int argc, char* argv[]){
    ezxml_t xml, trans_tag,  mode_tag;
    int num_of_trans = 0, num_of_modes = 0, num_v;
    int trans_id = 0;
    int cpu;
    int domain;
    struct vcpu* vcpus;
    //const char* s; //temp string
    char *unchanged = NULL, *changed = NULL, *old = NULL, *new = NULL; // points to extracted texts
    const char* xml_header = "<?xml versdion=\"1.0\"?>";

    if ( argc != 4)
        return fprintf(stderr, "usage: %s rules sys output_path\n", argv[0]);

    extract_type_transition_text(argv[1], &unchanged, &changed, &old, &new);

    xml = ezxml_parse_file(argv[2]);
    if ( xml == NULL )
    {
        printf("Failed to open the sys file\n");
        error();
    }

    cpu = atoi(get_attr(xml, "cpu"));
    //printf("rtds is on cpu %d\n", cpu);
    domain = atoi(get_attr(xml, "domain"));
    //printf("rtds is domain %d\n", domain);

    /* get number of modes and transitions*/
    for ( mode_tag = ezxml_child(xml, "mode"); mode_tag; mode_tag = mode_tag->next )
        num_of_modes++;

    if ( num_of_modes == 0 )
        return fprintf(stderr, "system xml file cannot have 0 number of modes\n");

    //printf("mc_sys: there are %d modes in this sys.\n", num_of_modes);

    for ( trans_tag = ezxml_child(xml, "trans"); trans_tag; trans_tag = trans_tag->next )
        num_of_trans++;

    if ( num_of_trans == 0 )
        return fprintf(stderr, "system xml file cannot have 0 number of transitions\n");

    //printf("mc_sys: there are %d transitions in this sys.\n", num_of_trans);

    /* iterate through all transitions, write one xml per transition */
    for ( trans_tag = ezxml_child(xml, "trans"); trans_tag; trans_tag = trans_tag->next )
    {
        FILE *fp;
        char name[50];
        char id_str[10];
        int i, src, dst;

        /* prepare file to write out */
        sprintf(id_str, "%d", trans_id++);
        memcpy(name, argv[3], strlen(argv[3])+1);
        strcat(name, "sys_trans");
        strcat(name,id_str);
        strcat(name,".xml");
        fp = fopen(name, "w");
        if ( fp == NULL )
        {
            printf("failed to create file %s\n", name);
            error();
        }

        /* print header for a transition */
        fprintf(fp, "%s\n",xml_header);
        fprintf(fp, "<request domain=\"%d\" cpu =\"%d\">\n", domain, cpu);

        
        /* writing all vcpus for a transition */
        src = atoi(get_attr(trans_tag, "src"));
        dst = atoi(get_attr(trans_tag, "dst"));
        //printf("parsing transition from %d to %d\n", src, dst);
        vcpus = get_vcpus_from_transition(xml, src, dst, &num_v);
        for ( i = 0; i< num_v; i++ )
        {
            /* get vcpu type and id from transition */
            const int vcpu_id = vcpus[i].id;
            char* vcpu_type = get_vcpu_type(vcpus[i].type);

            /* print per vcpu params */
            fprintf(fp, "    <vcpu type=\"%s\" id=\"%d\">\n", vcpu_type, vcpu_id);

            if ( strcmp(vcpu_type,"unchanged") == 0 )
                fprintf(fp, "%s", unchanged);
            else if ( strcmp(vcpu_type,"changed") == 0 )
            {
                fprintf(fp, "%s", changed);
                fprintf(fp, "    <period>%"PRId64"<period/>\n", vcpus[i].period);
                fprintf(fp, "    <budget>%"PRId64"<budget/>\n", vcpus[i].budget);
                fprintf(fp, "    <criticality>%d<criticality/>\n", vcpus[i].crit);
            }
            else if ( strcmp(vcpu_type,"new") == 0 )
            {
                fprintf(fp, "%s", new);
                fprintf(fp, "    <period>%"PRId64"<period/>\n", vcpus[i].period);
                fprintf(fp, "    <budget>%"PRId64"<budget/>\n", vcpus[i].budget);
                fprintf(fp, "    <criticality>%d<criticality/>\n", vcpus[i].crit);
            }
            else if ( strcmp(vcpu_type,"old") == 0 )
                fprintf(fp, "%s", old);

            fprintf(fp, "    </vcpu>\n");
        }
        fprintf(fp, "</request>");
        
        fclose(fp);
        printf("created %s\n", name);
    }

    fprintf(stderr, "%s\n", ezxml_error(xml));
}
