/* 
 * 
 */
#include <unistd.h>
#include <getopt.h>
#include <sqlite3.h>

#include <dag_recorder.h>
#include <dag_recorder_impl.h>

const char * sql_create_nodes
= ("create table nodes("
   "n_idx,"
   "kind,"
   "in_edge_kind,"
   "start_t,"
   "end_t,"
   "est,"
   "t_1,"
   "t_inf,"
   "first_ready_t,"
   "last_start_t,"
   "t_ready_end,"
   "t_ready_create,"
   "t_ready_create_cont,"
   "t_ready_wait_cont,"
   "node_count_create_task,"
   "node_count_wait_tasks,"
   "node_count_end_task,"
   "edge_count_end,"
   "edge_count_create,"
   "edge_count_create_cont,"
   "edge_count_wait_cont,"
   "cur_node_count,"
   "min_node_count,"
   "n_child_tasks,"
   "worker,"
   "cpu,"
   "start_file,"
   "start_line,"
   "end_file,"
   "end_line"
   ")"
   );

const char * sql_create_nodes_t_index
= "create index nodes_t_index on nodes(start_t,end_t)";
  
const char * sql_create_edges
= ("create table edges("
   "e_idx,"
   "kind,"
   "u,"
   "v"
   ")");

const char * sql_create_strings
= "create table strings(idx,file)";

const char * sql_create_strings_idx_index = 
"create index strings_idx on strings(idx)";

const char * sql_create_basics 
= "create table basics(start_clock,n_workers)";

static struct option dag2a_options[] = {
  {"dot",         required_argument, 0, 0 },
  {"stat",        required_argument, 0, 0 },
  {"parallelism", required_argument, 0, 0 },
  {"sqlite",      required_argument, 0, 'o' },
  {"text",        required_argument, 0, 0 },
  {"nodes",       required_argument, 0, 0 },
  {"edges",       required_argument, 0, 0 },
  {"strings",     required_argument, 0, 0 },
  {"help",        no_argument, 0, 0 },
  {0,         0,                 0,  0 }
};

static void usage(const char * progname) {
#define U printf
  U("SYNOPSIS\n");
  U("  %s [options] dag_file\n", progname);
  U("\n");
  U("  options:\n");
  U("    --dot filename\n");
  U("    --stat filename\n");
  U("    --parallelism filename\n");
  U("    -o,--sqlite filename\n");
  U("    --text filename\n");
  //U("    --nodes filename\n");
  //U("    --edges filename\n");
  //U("    --strings filename\n");
  U("DESCRIPTION\n");
  U("  %s converts .dag file generated by the dag recorder\n", 
    progname);
  U("  into various other formats, most importantly to text\n"
    "  and sqlite3 database for further analysis\n");
#undef U
}

static int parse_args(int argc, char ** argv,
		      const char ** filename_p) {
  dr_options opts[1];
  dr_options_default(opts);
  /* over write some default values */
  opts->dot_file = 0;	   /* no dot files */
  opts->stat_file = 0;	   /* no stat files */
  opts->gpl_file = 0;	/* no parallelism files */
  opts->text_file = 0;	   /* no text files */

  dr_opts_init(opts);
  dr_options * o = &GS.opts;
  const char * progname = argv[0];
  
  while (1) {
    int idx;
    int c = getopt_long(argc, argv, "o:", dag2a_options, &idx);
    if (c == -1) break;
    switch (c) {
    case 0: 
      {
	const char * name = dag2a_options[idx].name;
	const char * a = optarg;
	if (strcmp(name, "dot") == 0) {
	  o->dot_file = strdup(a);
	} else if (strcmp(name, "stat") == 0) {
	  o->stat_file = strdup(a);
	} else if (strcmp(name, "parallelism") == 0) {
	  o->gpl_file = strdup(a);
	} else if (strcmp(name, "sqlite") == 0) {
	  o->sqlite_file = strdup(a);
	} else if (strcmp(name, "text") == 0) {
	  o->text_file = strdup(a);
	} else if (strcmp(name, "nodes") == 0) {
	  o->nodes_file = strdup(a);
	} else if (strcmp(name, "edges") == 0) {
	  o->edges_file = strdup(a);
	} else if (strcmp(name, "strings") == 0) {
	  o->strings_file = strdup(a);
	} else if (strcmp(name, "help") == 0) {
	  usage(argv[0]);
	  return 0;
	} else {
	  fprintf(stderr, "?? BUG wrong option %s\n", name);
	  exit(1);		/* NG */
	}
	break;
      }
    case 'o':
      {
	const char * a = optarg;
	o->sqlite_file = strdup(a);
	break;
      }
    case '?': 
      {
	return 0;
      }
    default:
      {
	fprintf(stderr, "?? BUG wrong character code 0%o ??\n", c);
	exit(1);			/* NG */
      }
    }
  }

  if (optind + 1 == argc) {
    *filename_p = strdup(argv[optind]);
  } else if (optind + 1 < argc) {
    /* two or more filenames */
    fprintf(stderr, 
	    "%s: error: you can specify only one"
	    " DAG file (%d given)\n", 
	    progname, argc - optind);
    return 0;
  } else {
    /* no filename */
    fprintf(stderr, 
	    "error: you must specify an input DAG file\n");
    fprintf(stderr, 
	    "Usage:\n"
	    " %s [options] dag_file\n"
	    "Options:\n"
	    "  --sqlite F.sqlite :\n"
	    "    write sqlite file to F.sqlite (default : (do not write))\n"
	    "  --stat F.stat :\n"
	    "    write overall stat to F.stat (default : 00dr.stat)\n"
	    "  --parallelism F.gpl :\n"
	    "    write parallelism profile (gnuplot file) to F.gpl (default : 00dr.gpl)\n"
	    "  --text F.txt :\n"
	    "    write text-formatted DAG file to F.txt (default : (do not write))\n"
	    "Example:\n"
	    "  %s --sqlite 00dr.sqlite --parallelism \"\" --stat \"\" 00dr.dag\n",
	    progname, progname);
    return 0;
  }
  return 1;			/* OK */
}

static int 
do_sqlite3_(sqlite3 * db, const char * sql,
	    const char * file, int line) {
  if (1) {
    char * errmsg = 0;
    if (sqlite3_exec(db, sql, 0, 0, &errmsg) != SQLITE_OK) {
      fprintf(stderr, "%s:%d:error: error during query: %s (%s)\n",
	      file, line, sql, errmsg);
      return 0;
    }
    return 1;
  } else {
    sqlite3_stmt * stmt = 0;
    int r = sqlite3_prepare_v2(db, sql, strlen(sql), &stmt, NULL);
    if (r != SQLITE_OK) {
      fprintf(stderr, 
	      "%s:%d: failed to prepare query: %s (%s)\n", 
	      file, line, sql, sqlite3_errmsg(db));
      return 0;
    }
    r = sqlite3_step(stmt);
    while (r == SQLITE_ROW) {
      r = sqlite3_step(stmt);
    }
    if (r != SQLITE_DONE) {
      fprintf(stderr, 
	      "%s:%d: error during query: %s (%s)\n", 
	      file, line, sql, sqlite3_errmsg(db));
      return 0;
    }
    r = sqlite3_finalize(stmt);
    if (r != SQLITE_OK) {
      fprintf(stderr, 
	      "%s:%d: error when finalizing query: %s (%s)\n", 
	      file, line, sql, sqlite3_errmsg(db));
      return 0;
    }
    return 1;
  }
}

#define do_sqlite3(db, sql) do_sqlite3_(db, sql, __FILE__, __LINE__)

static int 
blong_(sqlite3 * db, sqlite3_stmt * stmt, 
       int col, long x,
       const char * file, int line) {
  int r = sqlite3_bind_int64(stmt, col, x);
  if (r == SQLITE_OK) {
    return 1;
  } else {
    fprintf(stderr, "%s:%d:error: could not bind long value %s\n",
	    file, line, sqlite3_errmsg(db));
    return 0;
  }
}

static int 
bint_(sqlite3 * db, sqlite3_stmt * stmt, 
      int col, int x,
      const char * file, int line) {
  int r = sqlite3_bind_int(stmt, col, x);
  if (r == SQLITE_OK) {
    return 1;
  } else {
    fprintf(stderr, "%s:%d:error: could not bind int value %s\n",
	    file, line, sqlite3_errmsg(db));
    return 0;
  }
}

static int 
btext_(sqlite3 * db, sqlite3_stmt * stmt, 
       int col, const char * x,
       const char * file, int line) {
  int r = sqlite3_bind_text(stmt, col, x, -1, SQLITE_STATIC);
  if (r == SQLITE_OK) {
    return 1;
  } else {
    fprintf(stderr, "%s:%d:error: could not bind text value %s\n",
	    file, line, sqlite3_errmsg(db));
    return 0;
  }
}

#define blong(x) blong_(db, stmt, col++, x , __FILE__, __LINE__)
#define bint(x)  bint_(db, stmt, col++, x , __FILE__, __LINE__)
#define btext(x) btext_(db, stmt, col++, x , __FILE__, __LINE__)

static int
insert_into_nodes(sqlite3 * db, sqlite3_stmt * stmt, 
		  long idx, dr_pi_dag_node * g) {
  int col = 1;
  int r = sqlite3_reset(stmt);
  if (r != SQLITE_OK) {
    fprintf(stderr, "%s:%d:bug: sqlite3_reset failed (%s)\n", 
	    __FILE__, __LINE__, sqlite3_errmsg(db));
    return 0;
  }
  if (!blong(idx)) return 0;
  if (!btext(dr_dag_node_kind_to_str(g->info.kind))) return 0;
  if (!btext(dr_dag_edge_kind_to_str(g->info.in_edge_kind))) return 0;
  if (!blong(g->info.start.t)) return 0;
  if (!blong(g->info.end.t)) return 0;
  if (!blong(g->info.est)) return 0;
  if (!blong(g->info.t_1)) return 0;
  if (!blong(g->info.t_inf)) return 0;
  if (!blong(g->info.first_ready_t)) return 0;
  if (!blong(g->info.last_start_t)) return 0;
  if (!blong(g->info.t_ready[dr_dag_edge_kind_end])) return 0;
  if (!blong(g->info.t_ready[dr_dag_edge_kind_create])) return 0;
  if (!blong(g->info.t_ready[dr_dag_edge_kind_create_cont])) return 0;
  if (!blong(g->info.t_ready[dr_dag_edge_kind_wait_cont])) return 0;
  if (!blong(g->info.logical_node_counts[dr_dag_node_kind_create_task])) return 0;
  if (!blong(g->info.logical_node_counts[dr_dag_node_kind_wait_tasks])) return 0;
  if (!blong(g->info.logical_node_counts[dr_dag_node_kind_end_task])) return 0;
  if (!blong(g->info.logical_edge_counts[dr_dag_edge_kind_end])) return 0;
  if (!blong(g->info.logical_edge_counts[dr_dag_edge_kind_create])) return 0;
  if (!blong(g->info.logical_edge_counts[dr_dag_edge_kind_create_cont])) return 0;
  if (!blong(g->info.logical_edge_counts[dr_dag_edge_kind_wait_cont])) return 0;
  if (!blong(g->info.cur_node_count)) return 0;
  if (!blong(g->info.min_node_count)) return 0;
  if (!blong(g->info.n_child_create_tasks)) return 0;
  if (!bint(g->info.worker)) return 0;
  if (!bint(g->info.cpu)) return 0;
  if (!blong(g->info.start.pos.file_idx)) return 0;
  if (!blong(g->info.start.pos.line)) return 0;
  if (!blong(g->info.end.pos.file_idx)) return 0;
  if (!blong(g->info.end.pos.line)) return 0;

  if (0) {
    r = SQLITE_DONE;
  } else {
    r = sqlite3_step(stmt);
    while (r == SQLITE_ROW) {
      r = sqlite3_step(stmt);
    }
  }
  if (r != SQLITE_DONE) {
    fprintf(stderr, 
	    "error during query: %s (%s)\n", 
	    sqlite3_sql(stmt), sqlite3_errmsg(db));
    return 0;
  }
  return 1;
}

#define PROGRESS_BAR_WIDTH 40
static int 
show_progress(long i, long n, int progress, int progress_width) {
  int p = (n ? (progress_width * (double)i) / (double)n : progress_width);
  int c;
  for (c = progress; c < p; c++) { putchar('.'); fflush(stdout); }
  return p;
}

static int 
gen_sqlite3_create_nodes_table(dr_pi_dag * G, sqlite3 * db) {
  if (!do_sqlite3(db, sql_create_nodes)) return 0;
  if (!do_sqlite3(db, sql_create_nodes_t_index)) return 0;

  long n = G->n;
  long i;
  const char * insert_sql = 
    "insert into nodes values("
    "?,?,?,?,?,?,?,?,?,?,"
    "?,?,?,?,?,?,?,?,?,?,"
    "?,?,?,?,?,?,?,?,?,?)";
  sqlite3_stmt * insert_stmt = 0;
  int r = sqlite3_prepare(db, insert_sql, strlen(insert_sql), 
			  &insert_stmt, NULL);
  if (r != SQLITE_OK) {
    fprintf(stderr, 
	    "failed to prepare query: %s (%s)\n", 
	    insert_sql, sqlite3_errmsg(db));
    return 0;
  }
  int progress_width = PROGRESS_BAR_WIDTH;
  int progress = 0;
  printf("nodes:   ");
  for (i = 0; i < n; i++) {
    dr_pi_dag_node * g = &G->T[i];
    progress = show_progress(i, n, progress, progress_width);
    if (!insert_into_nodes(db, insert_stmt, i, g)) return 0;
  }
  progress = show_progress(i, n, progress, progress_width);
  assert(progress == progress_width);
  printf("\n");

  r = sqlite3_finalize(insert_stmt);
  if (r != SQLITE_OK) {
    fprintf(stderr, 
	    "error when finalizing query: %s (%s)\n", 
	    sqlite3_sql(insert_stmt), sqlite3_errmsg(db));
    return 0;
  }
  return 1;
}

static int
insert_into_edges(sqlite3 * db, sqlite3_stmt * stmt, 
		  long idx, dr_pi_dag_edge * e) {
  int col = 1;
  int r = sqlite3_reset(stmt);
  if (r != SQLITE_OK) {
    fprintf(stderr, "%s:%d:bug: sqlite3_reset failed (%s)\n", 
	    __FILE__, __LINE__, sqlite3_errmsg(db));
    return 0;
  }
  if (!blong(idx)) return 0;
  if (!btext(dr_dag_edge_kind_to_str(e->kind))) return 0;
  if (!blong(e->u)) return 0;
  if (!blong(e->v)) return 0;

  r = sqlite3_step(stmt);
  while (r == SQLITE_ROW) {
    r = sqlite3_step(stmt);
  }
  if (r != SQLITE_DONE) {
    fprintf(stderr, 
	    "error during query: %s (%s)\n", 
	    sqlite3_sql(stmt), sqlite3_errmsg(db));
    return 0;
  }
  return 1;
}

static int 
gen_sqlite3_create_edges_table(dr_pi_dag * G, sqlite3 * db) {
  if (!do_sqlite3(db, sql_create_edges)) return 0;

  long m = G->m;
  long i;
  const char * insert_sql = 
    "insert into edges values(?,?,?,?)";
  sqlite3_stmt * insert_stmt = 0;
  int r = sqlite3_prepare(db, insert_sql, strlen(insert_sql), 
			  &insert_stmt, NULL);
  if (r != SQLITE_OK) {
    fprintf(stderr, 
	    "failed to prepare query: %s (%s)\n", 
	    insert_sql, sqlite3_errmsg(db));
    return 0;
  }
  int progress_width = PROGRESS_BAR_WIDTH;
  int progress = 0;
  printf("edges:   ");
  for (i = 0; i < m; i++) {
    dr_pi_dag_edge * e = &G->E[i];
    progress = show_progress(i, m, progress, progress_width);
    if (!insert_into_edges(db, insert_stmt, i, e)) return 0;
  }
  progress = show_progress(i, m, progress, progress_width);
  assert(progress == progress_width);
  printf("\n");

  r = sqlite3_finalize(insert_stmt);
  if (r != SQLITE_OK) {
    fprintf(stderr, 
	    "error when finalizing query: %s (%s)\n", 
	    sqlite3_sql(insert_stmt), sqlite3_errmsg(db));
    return 0;
  }
  return 1;
}

static int
insert_into_strings(sqlite3 * db, sqlite3_stmt * stmt, 
		    long idx, const char * s) {
  int col = 1;
  int r = sqlite3_reset(stmt);
  if (r != SQLITE_OK) {
    fprintf(stderr, "%s:%d:bug: sqlite3_reset failed (%s)\n", 
	    __FILE__, __LINE__, sqlite3_errmsg(db));
    return 0;
  }
  if (!blong(idx)) return 0;
  if (!btext(s)) return 0;

  r = sqlite3_step(stmt);
  while (r == SQLITE_ROW) {
    r = sqlite3_step(stmt);
  }
  if (r != SQLITE_DONE) {
    fprintf(stderr, 
	    "error during query: %s (%s)\n", 
	    sqlite3_sql(stmt), sqlite3_errmsg(db));
    return 0;
  }
  return 1;
}

static int
insert_into_basics(sqlite3 * db, sqlite3_stmt * stmt, 
		   dr_clock_t start_clock, long n_workers) {
  int col = 1;
  int r = sqlite3_reset(stmt);
  if (r != SQLITE_OK) {
    fprintf(stderr, "%s:%d:bug: sqlite3_reset failed (%s)\n", 
	    __FILE__, __LINE__, sqlite3_errmsg(db));
    return 0;
  }
  if (!blong(start_clock)) return 0;
  if (!blong(n_workers)) return 0;

  r = sqlite3_step(stmt);
  while (r == SQLITE_ROW) {
    r = sqlite3_step(stmt);
  }
  if (r != SQLITE_DONE) {
    fprintf(stderr, 
	    "error during query: %s (%s)\n", 
	    sqlite3_sql(stmt), sqlite3_errmsg(db));
    return 0;
  }
  return 1;
}

static int 
gen_sqlite3_create_strings_table(dr_pi_dag * G, sqlite3 * db) {
  if (!do_sqlite3(db, sql_create_strings)) return 0;
  if (!do_sqlite3(db, sql_create_strings_idx_index)) return 0;

  dr_pi_string_table * S = G->S;
  const char * C = S->C;
  long n = S->n;
  long i;
  const char * insert_sql = 
    "insert into strings values(?,?)";
  sqlite3_stmt * insert_stmt = 0;
  int r = sqlite3_prepare(db, insert_sql, strlen(insert_sql), 
			  &insert_stmt, NULL);
  if (r != SQLITE_OK) {
    fprintf(stderr, 
	    "failed to prepare query: %s (%s)\n", 
	    insert_sql, sqlite3_errmsg(db));
    return 0;
  }
  int progress_width = PROGRESS_BAR_WIDTH;
  int progress = 0;
  printf("strings: ");
  for (i = 0; i < n; i++) {
    const char * s = C + S->I[i];
    progress = show_progress(i, n, progress, progress_width);
    if (!insert_into_strings(db, insert_stmt, i, s)) return 0;
  }
  progress = show_progress(i, n, progress, progress_width);
  assert(progress == progress_width);
  printf("\n");
  r = sqlite3_finalize(insert_stmt);
  if (r != SQLITE_OK) {
    fprintf(stderr, 
	    "error when finalizing query: %s (%s)\n", 
	    sqlite3_sql(insert_stmt), sqlite3_errmsg(db));
    return 0;
  }
  return 1;
}

static int 
gen_sqlite3_create_basics_table(dr_pi_dag * G, sqlite3 * db) {
  if (!do_sqlite3(db, sql_create_basics)) return 0;

  const char * insert_sql = 
    "insert into basics values(?,?)";
  sqlite3_stmt * insert_stmt = 0;
  int r = sqlite3_prepare(db, insert_sql, strlen(insert_sql), 
			  &insert_stmt, NULL);
  if (r != SQLITE_OK) {
    fprintf(stderr, 
	    "failed to prepare query: %s (%s)\n", 
	    insert_sql, sqlite3_errmsg(db));
    return 0;
  }
  int progress_width = PROGRESS_BAR_WIDTH;
  int progress = 0;
  printf("basics:  ");
  if (!insert_into_basics(db, insert_stmt, G->start_clock, G->num_workers)) return 0;
  progress = show_progress(1, 1, progress, progress_width);
  assert(progress == progress_width);
  printf("\n");
  r = sqlite3_finalize(insert_stmt);
  if (r != SQLITE_OK) {
    fprintf(stderr, 
	    "error when finalizing query: %s (%s)\n", 
	    sqlite3_sql(insert_stmt), sqlite3_errmsg(db));
    return 0;
  }
  return 1;
}

static int dr_pi_dag_gen_sqlite3(dr_pi_dag * G, sqlite3 * db) {
  if (!do_sqlite3(db, "begin transaction")) return 0;
  if (!gen_sqlite3_create_basics_table(G, db)) return 0;
  if (!gen_sqlite3_create_nodes_table(G, db)) return 0;
  if (!gen_sqlite3_create_edges_table(G, db)) return 0;
  if (!gen_sqlite3_create_strings_table(G, db)) return 0;
  printf("commiting\n");
  if (!do_sqlite3(db, "end transaction")) return 0;
  return 1;
}


static int dr_gen_sqlite3(dr_pi_dag * G) {
  const char * sqlite3_file = GS.opts.sqlite_file;
  if (sqlite3_file) {
    fprintf(stderr, "writing sqlite3 to %s\n", sqlite3_file);
  } else {
    fprintf(stderr, "not writing sqlite3 file\n");
    return 1;	/* OK */
  }
  sqlite3 * db = NULL;

  unlink(sqlite3_file);

  if (sqlite3_open(sqlite3_file, &db) != SQLITE_OK) {
    fprintf(stderr, 
	    "dr_gen_sqlite3: could not open %s (%s)\n", 
	    sqlite3_file, sqlite3_errmsg(db));
    return 0;			/* NG */
  }
  return dr_pi_dag_gen_sqlite3(G, db);
}


static int read_and_analyze_dag(const char * filename) {
  dr_pi_dag * G = dr_read_dag(filename);
  if (G) {
    if (dr_gen_basic_stat(G) == 0) return 0;
    if (dr_gen_gpl(G) == 0) return 0;
    if (dr_gen_dot(G) == 0) return 0;
    if (dr_gen_text(G) == 0) return 0;
    if (dr_gen_sqlite3(G) == 0) return 0;
    return 1;
  } else {
    return 0;
  }
}

int main(int argc, char ** argv) {
  const char * filename = 0;
  if (parse_args(argc, argv, &filename) == 0) {
    return 1;			/* NG */
  } else if (read_and_analyze_dag(filename)) {
    return 0;			/* OK */
  } else {
    return 1;			/* NG */
  }
}
