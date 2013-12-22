/* 
 * gen_dot.c
 */

#include <errno.h>
#include <string.h>
#include "dag_recorder_impl.h"

static const char * 
dr_node_kind_str(dr_dag_node_kind_t kind) {
  switch (kind) {
  case dr_dag_node_kind_create_task: return "create_task";
  case dr_dag_node_kind_wait_tasks:  return "wait_tasks";
  case dr_dag_node_kind_end_task:    return "end_task";
  case dr_dag_node_kind_section:     return "section";
  case dr_dag_node_kind_task:        return "task";
  default : (void)dr_check(0);
  }
  return (const char *)0;
}

static void 
dr_pi_dag_node_gen_dot(dr_pi_dag_node * g, dr_pi_dag * G, FILE * wp) {
  fprintf(wp, 
	  "/* node: %ld edges: %ld-%ld */\n"
	  "T%lu [label=\"%s\\n"
	  "%llu-%llu est=%llu\\n"
	  "W=%llu/%llu,sz=%ld/%ld by %d on %d\"];\n",
	  g - G->T, g->edges_begin, g->edges_end,
	  g - G->T,
	  dr_node_kind_str(g->info.kind),
	  g->info.start, 
	  g->info.end, 
	  g->info.est, 
	  g->info.t_1, g->info.t_inf,
	  g->info.n_nodes, g->info.n_edges, 
	  g->info.worker, g->info.cpu);
}

static void
dr_pi_dag_edge_gen_dot(dr_pi_dag_edge * e, FILE * wp) {
  fprintf(wp, "T%lu -> T%lu;\n", e->u, e->v);
}

static void 
dr_pi_dag_gen_dot(dr_pi_dag * G, FILE * wp) {
  long n = G->n;
  long m = G->m;
  dr_pi_dag_node * T = G->T;
  dr_pi_dag_edge * E = G->E;
  long i;
  fprintf(wp, "digraph G {\n");
  for (i = 0; i < n; i++) {
    dr_pi_dag_node * g = &T[i];
    if (g->info.kind < dr_dag_node_kind_section
	|| g->subgraphs_begin_offset == g->subgraphs_end_offset) {
      dr_pi_dag_node_gen_dot(g, G, wp);
    }
  }
  for (i = 0; i < m; i++) {
    dr_pi_dag_edge_gen_dot(&E[i], wp);
  }
  fprintf(wp, "}\n");
}

int dr_gen_dot(dr_pi_dag * G) {
  FILE * wp = NULL;
  int must_close = 0;
  const char * filename = GS.opts.dot_file;
  if (filename) {
    if (strcmp(filename, "-") == 0) {
      wp = stdout;
    } else {
      wp = fopen(filename, "wb");
      if (!wp) { 
	fprintf(stderr, "fopen: %s (%s)\n", strerror(errno), filename); 
	return 0;
      }
      must_close = 1;
    }
  }
  if (wp) {
    dr_pi_dag_gen_dot(G, wp);
  }
  if (must_close) fclose(wp);
}
