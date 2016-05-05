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

void extract_type_transition_text(const char* path, char* unchanged,
                                  char* changed, char* old, char* new)
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
    changed = types[0];
    unchanged = types[1];
    new = types[2];
    old = types[3];
    printf("extracted changed:\n%s", changed);
    printf("extracted unchanged:\n%s", unchanged);
    printf("extracted new:\n%s", new);
    printf("extracted old:\n%s", old);
    fclose(fp);
}

int main(int argc, char* argv[]){
    ezxml_t xml, trans_tag;
    int num_of_trans = 0;
    //const char* s; //temp string
    char *unchanged = NULL, *changed = NULL, *old = NULL, *new = NULL; // points to extracted texts

    if ( argc != 3)
        return fprintf(stderr, "usage: %s xmlfile output_path\n", argv[0]);

    extract_type_transition_text(argv[1], unchanged, changed, old, new);

    xml = ezxml_parse_file(argv[1]);

    for ( trans_tag = ezxml_child(xml, "trans"); trans_tag; trans_tag = trans_tag->next )
        num_of_trans++;

    if ( num_of_trans == 0 )
        return fprintf(stderr, "system xml file cannot have 0 number of transitions\n");

    printf("mc_sys: there are %d transitions in this sys.\n", num_of_trans);

    fprintf(stderr, "%s\n", ezxml_error(xml));


}
