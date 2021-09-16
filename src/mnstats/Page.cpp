#include "Page.h"


Page::Page(node_id_t placement, node_id_t n_nodes) : placement(placement)
{
    node_accesses_since_placement.resize(n_nodes);
}

Page::~Page()
{
}
