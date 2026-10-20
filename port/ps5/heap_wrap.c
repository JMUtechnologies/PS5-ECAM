/*
 * PS5-ECAM: libc's allocator wrapped onto the platform layer's title heap (ps5platform/heap.h),
 * as the SDK fork's src/heap_wrap.c (Mihawk) wraps it, with a per-thread cache of small blocks in
 * front of it. tools/link.sh links this object ahead of the platform library, which then leaves
 * its own heap_wrap.o out: every symbol that object defines is defined here.
 *
 * The cache (heapThreadCache in ps5cemu.json, off by default): each malloc and free of the heap
 * takes its arena's lock and calls into the system library several times, and the console's are
 * slow next to a desktop's: Cemu's recompiler thread spent about a third of its time in them (the
 * first profile of 3.5.1, Breath of the Wild). A block of up to 1 KiB that a thread frees goes on
 * that thread's list for its size instead, and the thread's next allocation of that size takes it
 * back with no lock and no system call. Blocks stay ordinary heap blocks: a cached one is handed
 * out as it is, and freed to the heap when its thread's lists are full or the thread ends.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ps5platform/heap.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

void ps5p_heap_count_fallback(void);

/* libc's allocator, through the linker's --wrap. malloc_usable_size is weak:
 * a title that does not reference it links without its wrap. */
void *__real_malloc(size_t bytes);
void *__real_calloc(size_t count, size_t bytes);
void *__real_realloc(void *pointer, size_t bytes);
void __real_free(void *pointer);
int __real_posix_memalign(void **out, size_t alignment, size_t bytes);
__attribute__((weak)) size_t __real_malloc_usable_size(const void *pointer);

/* ------------------------------------------------------- the thread cache */

/* Set once as the app starts (main_ps5.cpp), before the game's threads. */
int g_ps5HeapThreadCache = 0;

#define CACHE_STEP 16u
#define CACHE_CLASSES 64u /* blocks up to 1 KiB */
#define CACHE_LARGEST (CACHE_STEP * CACHE_CLASSES)
#define CACHE_CLASS_BYTES 8192u /* a size's list holds at most this much */
#define CACHE_THREAD_BYTES (256u * 1024u) /* and a thread's lists together this much */

/* A thread's lists: class c holds blocks whose usable size is at least (c + 1) * 16 bytes, linked
 * through their first word. */
struct thread_cache {
   void *head[CACHE_CLASSES];
   uint16_t count[CACHE_CLASSES];
   size_t bytes;
};

static pthread_key_t cache_key;
static pthread_once_t cache_once = PTHREAD_ONCE_INIT;
static int cache_key_made;

static size_t
class_bytes(unsigned c)
{
   return (size_t)(c + 1) * CACHE_STEP;
}

static unsigned
class_limit(unsigned c)
{
   const size_t limit = CACHE_CLASS_BYTES / class_bytes(c);
   return limit < 4 ? 4 : limit > 128 ? 128 : (unsigned)limit;
}

/* The thread's blocks back to the heap as it ends. */
static void
flush_cache(void *value)
{
   struct thread_cache *const cache = value;
   for (unsigned c = 0; c < CACHE_CLASSES; c++) {
      void *block = cache->head[c];
      while (block) {
         void *const next = *(void **)block;
         ps5_heap_free(block);
         block = next;
      }
   }
   ps5_heap_free(cache);
}

static void
make_cache_key(void)
{
   if (pthread_key_create(&cache_key, flush_cache) == 0)
      __atomic_store_n(&cache_key_made, 1, __ATOMIC_RELEASE);
}

/* The calling thread's lists, made at its first use of them; NULL if they cannot be. */
static struct thread_cache *
thread_cache(void)
{
   if (!__atomic_load_n(&cache_key_made, __ATOMIC_ACQUIRE)) {
      pthread_once(&cache_once, make_cache_key);
      if (!__atomic_load_n(&cache_key_made, __ATOMIC_ACQUIRE))
         return NULL;
   }
   struct thread_cache *cache = pthread_getspecific(cache_key);
   if (!cache) {
      /* from the heap itself, not these wraps: no list to take it from yet */
      cache = ps5_heap_calloc(1, sizeof(*cache));
      if (cache && pthread_setspecific(cache_key, cache) != 0) {
         ps5_heap_free(cache);
         cache = NULL;
      }
   }
   return cache;
}

/* A block of at least `bytes` from the thread's lists, or from the heap in the size of its list
 * (so it goes back to that list when freed); NULL when neither has one. */
static void *
cached_malloc(size_t bytes)
{
   const unsigned c = bytes ? (unsigned)((bytes - 1) / CACHE_STEP) : 0;
   struct thread_cache *const cache = thread_cache();
   if (cache && cache->head[c]) {
      void *const block = cache->head[c];
      cache->head[c] = *(void **)block;
      cache->count[c]--;
      cache->bytes -= class_bytes(c);
      return block;
   }
   return ps5_heap_malloc(class_bytes(c));
}

/* A heap block onto the thread's list for its size, if it has room: false leaves it to the heap. */
static int
cache_free(void *pointer)
{
   const size_t usable = ps5_heap_usable_size(pointer);
   if (usable < CACHE_STEP)
      return 0;
   unsigned c = (unsigned)(usable / CACHE_STEP) - 1;
   if (c >= CACHE_CLASSES)
      return 0;
   struct thread_cache *const cache = thread_cache();
   if (!cache || cache->count[c] >= class_limit(c) || cache->bytes + class_bytes(c) > CACHE_THREAD_BYTES)
      return 0;
   *(void **)pointer = cache->head[c];
   cache->head[c] = pointer;
   cache->count[c]++;
   cache->bytes += class_bytes(c);
   return 1;
}

/* --------------------------------------------------------------- the wraps */

void *
__wrap_malloc(size_t bytes)
{
   if (g_ps5HeapThreadCache && bytes <= CACHE_LARGEST) {
      void *const pointer = cached_malloc(bytes);
      if (pointer)
         return pointer;
   }
   void *const pointer = ps5_heap_malloc(bytes);
   if (pointer)
      return pointer;
   ps5p_heap_count_fallback();
   return __real_malloc(bytes);
}

void *
__wrap_calloc(size_t count, size_t bytes)
{
   if (g_ps5HeapThreadCache && (bytes == 0 || count <= CACHE_LARGEST / bytes) && count * bytes <= CACHE_LARGEST) {
      void *const pointer = cached_malloc(count * bytes);
      if (pointer) {
         memset(pointer, 0, count * bytes);
         return pointer;
      }
   }
   void *const pointer = ps5_heap_calloc(count, bytes);
   if (pointer)
      return pointer;
   ps5p_heap_count_fallback();
   return __real_calloc(count, bytes);
}

void
__wrap_free(void *pointer)
{
   if (!pointer)
      return;
   if (ps5_heap_owns(pointer)) {
      if (!g_ps5HeapThreadCache || !cache_free(pointer))
         ps5_heap_free(pointer);
   } else {
      __real_free(pointer);
   }
}

size_t
__wrap_malloc_usable_size(const void *pointer)
{
   if (!pointer)
      return 0;
   if (ps5_heap_owns(pointer))
      return ps5_heap_usable_size(pointer);
   return __real_malloc_usable_size ? __real_malloc_usable_size(pointer) : 0;
}

void *
__wrap_realloc(void *pointer, size_t bytes)
{
   if (!pointer)
      return __wrap_malloc(bytes);
   /* FreeBSD's realloc gives a zero-size request a minimum-size object. */
   if (bytes == 0)
      bytes = 1;
   if (ps5_heap_owns(pointer)) {
      void *const moved = ps5_heap_realloc(pointer, bytes);
      if (moved)
         return moved;
      /* Direct memory is exhausted: move the block to libc. */
      void *const copy = __real_malloc(bytes);
      if (!copy)
         return NULL;
      ps5p_heap_count_fallback();
      const size_t kept = ps5_heap_usable_size(pointer);
      memcpy(copy, pointer, kept < bytes ? kept : bytes);
      ps5_heap_free(pointer);
      return copy;
   }
   void *const moved = __real_realloc(pointer, bytes);
   if (moved || !__real_malloc_usable_size)
      return moved;
   /* libc's heap is exhausted: move its block to the title heap. */
   void *const copy = ps5_heap_malloc(bytes);
   if (!copy)
      return NULL;
   const size_t kept = __real_malloc_usable_size(pointer);
   memcpy(copy, pointer, kept < bytes ? kept : bytes);
   __real_free(pointer);
   return copy;
}

void *
__wrap_reallocf(void *pointer, size_t bytes)
{
   void *const moved = __wrap_realloc(pointer, bytes);
   if (!moved)
      __wrap_free(pointer);
   return moved;
}

void *
__wrap_reallocarray(void *pointer, size_t count, size_t bytes)
{
   if (bytes != 0 && count > SIZE_MAX / bytes) {
      errno = ENOMEM;
      return NULL;
   }
   return __wrap_realloc(pointer, count * bytes);
}

int
__wrap_posix_memalign(void **out, size_t alignment, size_t bytes)
{
   if (alignment < sizeof(void *) || (alignment & (alignment - 1)) != 0)
      return EINVAL;
   void *const pointer = ps5_heap_memalign(alignment, bytes);
   if (pointer) {
      *out = pointer;
      return 0;
   }
   ps5p_heap_count_fallback();
   return __real_posix_memalign(out, alignment, bytes);
}

void *
__wrap_aligned_alloc(size_t alignment, size_t bytes)
{
   void *pointer = NULL;
   const int result =
      __wrap_posix_memalign(&pointer, alignment < sizeof(void *) ? sizeof(void *) : alignment, bytes);
   if (result != 0) {
      errno = result;
      return NULL;
   }
   return pointer;
}

void *
__wrap_memalign(size_t alignment, size_t bytes)
{
   return __wrap_aligned_alloc(alignment, bytes);
}

/* libc's getdelim grows the caller's buffer with libc's realloc, which must
 * never see a title-heap block: this one grows it with the wrap. */
ssize_t
__wrap_getdelim(char **line, size_t *capacity, int delimiter, FILE *stream)
{
   if (!line || !capacity || !stream) {
      errno = EINVAL;
      return -1;
   }
   if (!*line)
      *capacity = 0;
   size_t length = 0;
   for (;;) {
      const int c = fgetc(stream);
      if (c == EOF) {
         if (length == 0 || ferror(stream))
            return -1;
         break;
      }
      if (length + 2 > *capacity) {
         size_t grown = *capacity < 64 ? 128 : *capacity * 2;
         if (grown > (size_t)SSIZE_MAX) {
            errno = EOVERFLOW;
            return -1;
         }
         char *const moved = __wrap_realloc(*line, grown);
         if (!moved)
            return -1;
         *line = moved;
         *capacity = grown;
      }
      (*line)[length++] = (char)c;
      if (c == (unsigned char)delimiter)
         break;
   }
   (*line)[length] = '\0';
   return (ssize_t)length;
}

ssize_t
__wrap_getline(char **line, size_t *capacity, FILE *stream)
{
   return __wrap_getdelim(line, capacity, '\n', stream);
}
