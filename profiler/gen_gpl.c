/* 
 * gen_gpl.c
 */

#include <errno.h>
#include <string.h>
#include "dag_recorder_impl.h"

typedef enum {
  dr_node_state_running,
  dr_node_state_end,
  dr_node_state_create,
  dr_node_state_create_cont,
  dr_node_state_wait_cont,
  dr_node_state_max,
} dr_node_state_t;

typedef struct {
  /* a[0] : running */
  dr_clock_t t;
  int a[dr_node_state_max];
} dr_para_prof_history_entry;

typedef struct {
  /* this must be the first element */
  void (*process_event)(chronological_traverser * ct, dr_event evt);
  int counts[dr_node_state_max];
  dr_clock_t last_time;
  dr_para_prof_history_entry * history;
  long hist_sz;
  long n_hists;
} dr_para_prof;

static void 
dr_para_prof_process_event(chronological_traverser * pp, dr_event evt);

static void 
dr_para_prof_init(dr_para_prof * pp, long hist_sz, dr_clock_t last_time) {
  int i;
  for (i = 0; i < dr_node_state_max; i++) {
    pp->counts[i] = 0;
  }
  pp->process_event = dr_para_prof_process_event;
  pp->last_time = last_time;
  pp->hist_sz = hist_sz;
  pp->n_hists = 0;
  pp->history = (dr_para_prof_history_entry *)
    dr_malloc(sizeof(dr_para_prof_history_entry) * hist_sz);
}

static void 
dr_para_prof_add_hist(dr_para_prof * pp, dr_clock_t t) {
  long sz = pp->hist_sz;
  long n = pp->n_hists;
  dr_para_prof_history_entry * hist = pp->history;
  long last_t = (n ? hist[n-1].t : 0);
  if (last_t + (t - last_t) * (sz - n) >= pp->last_time) {
    int i;
    dr_para_prof_history_entry * h = &hist[n];
    assert(n < sz);
    h->t = t;
    for (i = 0; i < dr_node_state_max; i++) {
      h->a[i] = pp->counts[i];
    }
    pp->n_hists = n + 1;
  }
}

static void
dr_para_prof_check(dr_para_prof * pp) {
  int i;
  for (i = 0; i < dr_node_state_max; i++) {
    (void)dr_check(pp->counts[i] == 0);
  }
}

static void 
dr_para_prof_write_to_file(dr_para_prof * pp, FILE * wp) {
  long n = pp->n_hists;
  dr_para_prof_history_entry * h = pp->history;
  long i;
  int k;
  /* 
     dr_node_state_running,
     dr_node_state_end,
     dr_node_state_create,
     dr_node_state_create_cont,
     dr_node_state_wait_cont,
  */

  fprintf(wp,
	  "plot"
	  "  \"-\" u 1:2:3 w filledcurves title \"running\""
	  ", \"-\" u 1:2:3 w filledcurves title \"end\""
	  ", \"-\" u 1:2:3 w filledcurves title \"create\""
	  ", \"-\" u 1:2:3 w filledcurves title \"create cont\""
	  ", \"-\" u 1:2:3 w filledcurves title \"wait cont\""
	  "\n"
	  );
  for (k = 0; k < dr_node_state_max; k++) {
    for (i = 0; i < n; i++) {
      int j;
      int y = 0;
      if (i > 0) {
	int y_prev = 0;
	for (j = 0; j < k; j++) {
	  y_prev += h[i-1].a[j];
	}
	fprintf(wp, "%llu %d %d\n", h[i].t, y_prev, y_prev + h[i-1].a[k]);
      } else {
	fprintf(wp, "%llu %d %d\n", h[i].t, 0, 0);
      }
      for (j = 0; j < k; j++) {
	y += h[i].a[j];
      }
      fprintf(wp, "%llu %d %d\n", h[i].t, y, y + h[i].a[k]);
    }
    fprintf(wp, "e\n");
  }
  fprintf(wp, "pause -1\n");
}

static dr_node_state_t 
calc_node_state(dr_pi_dag_node * pred, dr_edge_kind_t ek) {
  if (!pred) return dr_node_state_create;
  switch (ek) {
  case dr_edge_kind_cont:
    switch (pred->info.last_node_kind) {
    case dr_dag_node_kind_create_task:
      return dr_node_state_create_cont;
    case dr_dag_node_kind_wait_tasks:
      return dr_node_state_wait_cont;
    case dr_dag_node_kind_end_task:
    default:
      (void)dr_check(0);
    }
  case dr_edge_kind_create:
    return dr_node_state_create;
  case dr_edge_kind_end:
    return dr_node_state_end;
  default:
    (void)dr_check(0);
  }
}

static void 
dr_para_prof_process_event(chronological_traverser * pp_, dr_event ev) {
  dr_para_prof * pp = (dr_para_prof *)pp_;
  switch (ev.kind) {
  case dr_event_kind_ready: {
    dr_node_state_t k = calc_node_state(ev.pred, ev.edge_kind);
    pp->counts[k]++;
    break;
  }
  case dr_event_kind_start: {
    dr_node_state_t k = calc_node_state(ev.pred, ev.edge_kind);
    pp->counts[k]--;
    pp->counts[dr_node_state_running]++;
    break;
  }
  case dr_event_kind_end: {
    pp->counts[dr_node_state_running]--;
    break;
  }
  default:
    assert(0);
    break;
  }
  dr_para_prof_add_hist(pp, ev.t);
}

void 
dr_gen_gpl(dr_pi_dag * G) {
  FILE * wp = NULL;
  int must_close = 0;
  const char * filename = GS.opts.gpl_file;
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
  dr_pi_dag_node * last = dr_pi_dag_node_last(G->T, G);
  dr_para_prof pp[1];
  dr_para_prof_init(pp, GS.opts.gpl_sz, last->info.end);
  dr_pi_dag_chronological_traverse(G, (chronological_traverser *)pp);
  dr_para_prof_check(pp);
  dr_para_prof_write_to_file(pp, wp);
  if (filename) fclose(wp);
}
