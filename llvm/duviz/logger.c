#include <graphviz/cgraph.h>
#include <graphviz/gvc.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_VALUE_LENGTH 11
#define NEW_FILENAME_PREFIX "new_"
#define VALUE_PREFIX "VALUE="
#define PLACEHOLDER_VALUE_LEN 1


void log_call(const char *Filename, uint64_t NodeId, int32_t Value) {
    FILE *OutFile = fopen(Filename, "r");

    char *OutFilenameNew = malloc(strlen(Filename) + sizeof(NEW_FILENAME_PREFIX));
    if (OutFilenameNew == NULL) {
        fclose(OutFile);
        return;
    }

    sprintf(OutFilenameNew, "%s%s", NEW_FILENAME_PREFIX, Filename);
    FILE *OutFileNew = fopen(OutFilenameNew, "w");

    if (OutFile && OutFileNew) {
        GVC_t *Gvc = gvContext();
        Agraph_t *Graph = agread(OutFile, NULL);
        
        Agnode_t *Node = agidnode(Graph, NodeId, false);
        const char *NodeLabel = agget(Node, "label");
        
        char *NewNodeLabel = malloc(strlen(NodeLabel) + MAX_VALUE_LENGTH - PLACEHOLDER_VALUE_LEN);
        if (NewNodeLabel != NULL) {
            // find where "VALUE=" starts
            size_t ValueOffset = strstr(NodeLabel, VALUE_PREFIX) - NodeLabel + (sizeof(VALUE_PREFIX) - 1);
            
            // copy everything up to "="
            memcpy(NewNodeLabel, NodeLabel, ValueOffset);
            NewNodeLabel[ValueOffset] = 0; // set c-style string end
            // write value as int after "="
            sprintf(NewNodeLabel + ValueOffset, "%d", Value);
            ValueOffset += PLACEHOLDER_VALUE_LEN; // skip initial placeholder
            // copy everything after "="
            strcat(NewNodeLabel, NodeLabel + ValueOffset);

            // set new label
            NodeLabel = agstrdup_html(Graph, NewNodeLabel);
            agset(Node, "label", NodeLabel);
            agstrfree(Graph, NodeLabel, true);
            free(NewNodeLabel);
            
            gvLayout(Gvc, Graph, "dot");
            gvRender(Gvc, Graph, "dot", OutFileNew);
            gvFreeLayout(Gvc, Graph);
            agclose(Graph);
        }

    }

    fclose(OutFileNew);
    fclose(OutFile);
    rename(OutFilenameNew, Filename);
    free(OutFilenameNew);
}

