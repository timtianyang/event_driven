#define XC_WANT_COMPAT_MAP_FOREIGN_API
#include <xenctrl.h>
#include <stdlib.h>
#include <xc_private.h>
#include "ezxml.h"

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
        if (strcmp(line, "    <rules>") == 0)
        {
            rules_found = 1;
            printf("found rules\n");
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
            while ( next_type || strcmp(line, "        </vcpu>") != 0)
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
                    else if ( strcmp(line, "        </vcpu>\n") != 0 )
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
    printf("extracted changed:\n%s", *changed);
    printf("extracted unchanged:\n%s", *unchanged);
    printf("extracted new:\n%s", *new);
    printf("extracted old:\n%s", *old);
    fclose(fp);
}

/*
 * arg1: sys_xml
 * arg2: output path, created xmls will be appended with numbers
 */
int main(int argc, char* argv[]){
    ezxml_t xml, trans_tag, vcpu_tag;
    int num_of_trans = 0;
    int trans_id = 0;
    int cpu;
    int domain;
    //const char* s; //temp string
    char *unchanged = NULL, *changed = NULL, *old = NULL, *new = NULL; // points to extracted texts
    const char* xml_header = "<?xml versdion=\"1.0\"?>";

    if ( argc != 3)
        return fprintf(stderr, "usage: %s xmlfile output_path\n", argv[0]);

    extract_type_transition_text(argv[1], &unchanged, &changed, &old, &new);

    xml = ezxml_parse_file(argv[1]);

    cpu = atoi(get_attr(xml, "cpu"));
    printf("rtds is on cpu %d\n", cpu);
    domain = atoi(get_attr(xml, "domain"));
    printf("rtds is domain %d\n", domain);

    for ( trans_tag = ezxml_child(xml, "trans"); trans_tag; trans_tag = trans_tag->next )
        num_of_trans++;

    if ( num_of_trans == 0 )
        return fprintf(stderr, "system xml file cannot have 0 number of transitions\n");

    printf("mc_sys: there are %d transitions in this sys.\n", num_of_trans);

    for ( trans_tag = ezxml_child(xml, "trans"); trans_tag; trans_tag = trans_tag->next )
    {
        FILE *fp;
        char name[50];
        char id_str[10];

        /* prepare file to write out */
        sprintf(id_str, "%d", trans_id++);
        memcpy(name, argv[2], strlen(argv[2])+1);
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
        for ( vcpu_tag = ezxml_child(trans_tag, "vcpu"); vcpu_tag; vcpu_tag = vcpu_tag->next )
        {
            /* get vcpu type and id from transition */
            const char* vcpu_id = get_attr(vcpu_tag, "id");
            const char* vcpu_type = get_attr(vcpu_tag, "type");

            /* print per vcpu params */
            fprintf(fp, "    <vcpu type=\"%s\" id=\"%s\">\n", vcpu_type, vcpu_id);

            if ( strcmp(vcpu_type,"unchanged") == 0 )
                fprintf(fp, "%s", unchanged);
            else if ( strcmp(vcpu_type,"changed") == 0 )
                fprintf(fp, "%s", changed);
            else if ( strcmp(vcpu_type,"new") == 0 )
                fprintf(fp, "%s", new);
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
