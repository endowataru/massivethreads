/*
 * nbody.cc
 */
#include <sys/types.h>
#include <sys/time.h>

#include "def.h"

void simulate_a_step(particle ** particles, int n_particles,  t_real dt)
{
  int t0, t1, t2, t3, t4;
  space * tree;
  
  t0 = current_real_time_milli();
  tree = generate_tree(particles, n_particles);
  t1 = current_real_time_milli();
  mass_momentum mm = tree->set_mass_and_cg();
  t2 = current_real_time_milli();
  printf("%d nodes in the tree\n", mm.n_nodes);
  set_accels(particles, n_particles, tree);
  t3 = current_real_time_milli();
  move_particles(particles, n_particles, dt);
  t4 = current_real_time_milli();
  printf("generate-tree: %d msec\n", t1 - t0);
  printf("set-mass-and-cg: %d msec\n", t2 - t1);
  printf("set-accels: %d msec\n", t3 - t2);
  printf("move-particles: %d sec\n", t4 - t3);
  printf("sum: %d sec\n", t4 - t0);
}

void dump_particles (particle **, int);

void nbody_main (int n_particles, int n_steps, t_real dt, int dump_p)
{
  printf("nbody_main\n");
  particle ** particles = generate_particles(n_particles);
  if (dump_p) dump_particles(particles, n_particles);
  for (int i = 0; i < n_steps; i++) {
    simulate_a_step(particles, n_particles, dt);
  }
  if (dump_p) dump_particles(particles, n_particles);
  printf("nbody_main end\n");
}

int main (int argc, char ** argv)
{
  if (argc != 3) {
    printf ("usage: nbody n_particles n_steps\n");
    exit(1);
  }
  int n_particles = atoi(argv[1]);
  int n_steps = atoi(argv[2]);
  int dump_p = 0;
  t_real dt = 0.1;
  
  printf("nbody_main %d %d\n", n_particles, n_steps);
  int t0 = current_real_time_milli();
  nbody_main(n_particles, n_steps, dt, dump_p);
  int t1 = current_real_time_milli();
  printf("TOTAL %d msec\n", t1 - t0);
  return 0;
}