// Test of the emutls fast path (the patched compiler-rt emutls.c, included so its table can be
// checked). TLS addresses are pure values to the optimizer (llvm.threadlocal.address), so each
// access here goes through a volatile function pointer: every one is a real __emutls_get_address.
#define _GNU_SOURCE
#include "emutls.c"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>

static _Thread_local unsigned long tl_a;
static _Thread_local unsigned long tl_b = 7;
static _Thread_local struct { unsigned long x[16]; } tl_big;
static _Thread_local double tl_d = 1.5;
static _Thread_local unsigned long tl_late; // first touched late: grows the thread's array

static unsigned long *addr_a(void) { return &tl_a; }
static unsigned long *addr_b(void) { return &tl_b; }
static unsigned long *addr_big(void) { return tl_big.x; }
static double *addr_d(void) { return &tl_d; }
static unsigned long *addr_late(void) { return &tl_late; }
static unsigned long *(*volatile get_a)(void) = addr_a;
static unsigned long *(*volatile get_b)(void) = addr_b;
static unsigned long *(*volatile get_big)(void) = addr_big;
static double *(*volatile get_d)(void) = addr_d;
static unsigned long *(*volatile get_late)(void) = addr_late;

static atomic_ulong failures, cached_workers;

static void *worker(void *arg) {
  const unsigned long id = (unsigned long)arg;
  if (*get_b() != 7 || *get_d() != 1.5 || *get_a() != 0 || *get_late() != 0)
    atomic_fetch_add(&failures, 1); // a fresh thread sees fresh values, whoever had its block before
  *get_b() = id + 7;
  get_big()[id % 16] = id;
  *get_d() = 1.5 + id;
  for (unsigned long round = 0; round < 5000; round++) {
    *get_a() = id * 3 + round;
    if (round == 2500)
      *get_late() = id;
    if (*get_a() != id * 3 + round || *get_b() != id + 7 || get_big()[id % 16] != id || *get_d() != 1.5 + id ||
        (round > 2500 && *get_late() != id))
      atomic_fetch_add(&failures, 1);
  }
  if (emutls_fast_find(emutls_fast_tcb()) != NULL)
    atomic_fetch_add(&cached_workers, 1);
  return NULL;
}

static unsigned cached_slots(void) {
  unsigned n = 0;
  for (unsigned i = 0; i < EMUTLS_FAST_SLOTS; i++)
    if (__atomic_load_n(&emutls_fast[i].tcb, __ATOMIC_RELAXED))
      n++;
  return n;
}

static double ns_per_access(int fast) {
  __ps5_emutls_fast = fast;
  *get_a() = 0; // fills the cache when on
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  unsigned long sum = 0;
  for (unsigned long i = 0; i < 20000000; i++)
    sum += *get_a();
  clock_gettime(CLOCK_MONOTONIC, &t1);
  __asm__ volatile("" : : "r"(sum));
  return ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 20000000.0;
}

int main(void) {
  const double slow = ns_per_access(0);
  const unsigned before = cached_slots();
  const double fast = ns_per_access(1);
  printf("an access: %.2f ns without the cache, %.2f ns with it; main cached %u -> %u\n", slow, fast, before, cached_slots());
  unsigned long next = 1;
  for (int batch = 0; batch < 300; batch++) {
    pthread_t threads[48];
    for (int i = 0; i < 48; i++)
      pthread_create(&threads[i], NULL, worker, (void *)(next++));
    for (int i = 0; i < 48; i++)
      pthread_join(threads[i], NULL);
    if (cached_slots() != 1) {
      if (atomic_load(&failures) < 5)
        printf("batch %d: %u slots cached after its threads ended\n", batch, cached_slots());
      atomic_fetch_add(&failures, 1);
    }
  }
  *get_a() = 42;
  if (*get_a() != 42)
    atomic_fetch_add(&failures, 1);
  printf("%lu threads, %lu cached, %lu failures\n", next - 1, (unsigned long)atomic_load(&cached_workers),
         (unsigned long)atomic_load(&failures));
  return atomic_load(&failures) ? 1 : 0;
}
