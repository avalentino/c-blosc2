/*
  Copyright (C) 2018  Francesc Alted
  http://blosc.org
  License: BSD 3-Clause (see LICENSE.txt)

  Example program showing how to operate with compressed buffers.

  To compile this program for synthetic data (default):

  $ gcc -fopenmp -O3 sum_openmp.c -o sum_openmp -lblosc

  To run:

  $ OMP_PROC_BIND=spread OMP_NUM_THREADS=4 ./sum_openmp
  Blosc version info: 2.0.0a6.dev ($Date:: 2018-05-18 #$)
  Sum for uncompressed data: 199950000000
  Sum time for uncompressed data: 0.0296 s, 25752.4 MB/s
  Compression ratio: 762.9 MB -> 14.2 MB (53.9x)
  Compression time: 0.741 s, 1029.3 MB/s
  Sum for *compressed* data: 199950000000
  Sum time for *compressed* data: 0.0313 s, 24339.2 MB/s

  To use real (rainfall) data:

  $ gcc-8 -DRAINFALL -fopenmp -Ofast sum_openmp.c -o sum_openmp

  And running it:

  $ OMP_PROC_BIND=spread OMP_NUM_THREADS=4 ./sum_openmp
  Blosc version info: 2.0.0a6.dev ($Date:: 2018-05-18 #$)
  Sum for uncompressed data:   29851430
  Sum time for uncompressed data: 0.0144 s, 26461.0 MB/s
  Compression ratio: 381.5 MB -> 80.3 MB (4.7x)
  Compression time: 0.378 s, 1010.4 MB/s
  Sum for *compressed* data:   29952800
  Sum time for *compressed* data: 0.0513 s, 7432.2 MB/s


*/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <errno.h>
#include <assert.h>
#include "blosc.h"

#define KB  1024.
#define MB  (1024*KB)
#define GB  (1024*MB)

#define N (100 * 1000 * 1000)
#define CHUNKSIZE (4 * 1000)
#define NCHUNKS (N / CHUNKSIZE)
#define NTHREADS 8
#define NITER 5
#ifdef RAINFALL
#define SYNTHETIC false
#else
#define SYNTHETIC true
#endif

#if SYNTHETIC == true
#define DTYPE int64_t
#define CLEVEL 9
#define CODEC BLOSC_BLOSCLZ
#else
#define DTYPE float
#define CLEVEL 9
#define CODEC BLOSC_LZ4
#endif


int main() {
  static DTYPE udata[N];
  DTYPE chunk_buf[CHUNKSIZE];
  size_t isize = CHUNKSIZE * sizeof(DTYPE);
  DTYPE sum, compressed_sum;
  int64_t nbytes, cbytes;
  blosc2_cparams cparams = BLOSC_CPARAMS_DEFAULTS;
  blosc2_dparams dparams = BLOSC_DPARAMS_DEFAULTS;
  blosc2_schunk* schunk;
  int i, j, nchunk;
  blosc_timestamp_t last, current;
  double ttotal, itotal;

  printf("Blosc version info: %s (%s)\n",
         BLOSC_VERSION_STRING, BLOSC_VERSION_DATE);

  // Fill the buffer for a chunk
  if (SYNTHETIC) {
    for (j = 0; j < CHUNKSIZE; j++) {
      chunk_buf[j] = j;
    }
  }
  else {
    struct stat info;
    const char *filegrid = "rainfall-grid-150x150.bin";
    if (stat(filegrid, &info) != 0) {
      printf("Grid file %s not found!", filegrid);
      exit(1);
    }
    char *cdata = malloc(info.st_size);

    FILE *f = fopen(filegrid, "rb");
    size_t blocks_read = fread(cdata, info.st_size, 1, f);
    assert(blocks_read == 1);
    fclose(f);

    int dsize = blosc_getitem(cdata, 0, CHUNKSIZE, chunk_buf);
    if (dsize < 0) {
      printf("blosc_getitem() error.  Error code: %d\n.  Probaly reading too much data?", dsize);
      exit(1);
    }
    free(cdata);
  }

  // Fill the uncompressed dataset with data chunks
  for (i = 0; i < N / CHUNKSIZE; i++) {
    for (j = 0; j < CHUNKSIZE; j++) {
      udata[i * CHUNKSIZE + j] = chunk_buf[j];
    }
  }

  // Reduce uncompressed dataset
  ttotal = 1e10;
  sum = 0;
  for (int n = 0; n < NITER; n++) {
    sum = 0;
    blosc_set_timestamp(&last);
#pragma omp parallel for reduction (+:sum)
    for (i = 0; i < N; i++) {
      sum += udata[i];
    }
    blosc_set_timestamp(&current);
    itotal = blosc_elapsed_secs(last, current);
    if (itotal < ttotal) ttotal = itotal;
  }
  printf("Sum for uncompressed data: %10.0f\n", (double)sum);
  printf("Sum time for uncompressed data: %.3g s, %.1f MB/s\n",
         ttotal, (isize * NCHUNKS) / (ttotal * (double)MB));

  // Create a super-chunk container for the compressed container
  cparams.typesize = sizeof(DTYPE);
  cparams.compcode = CODEC;
  cparams.clevel = CLEVEL;
  cparams.nthreads = 1;
  dparams.nthreads = 1;
  blosc_set_timestamp(&last);
  schunk = blosc2_new_schunk(cparams, dparams);
  for (nchunk = 0; nchunk < NCHUNKS; nchunk++) {
    for (i = 0; i < CHUNKSIZE; i++) {
      chunk_buf[i] = udata[i + nchunk * CHUNKSIZE];
    }
    blosc2_append_buffer(schunk, isize, chunk_buf);
  }
  blosc_set_timestamp(&current);
  ttotal = blosc_elapsed_secs(last, current);
  nbytes = schunk->nbytes;
  cbytes = schunk->cbytes;
  printf("Compression ratio: %.1f MB -> %.1f MB (%.1fx)\n",
         nbytes / MB, cbytes / MB, (1. * nbytes) / cbytes);
  printf("Compression time: %.3g s, %.1f MB/s\n",
         ttotal, nbytes / (ttotal * MB));

  int nthreads = NTHREADS;
  char* envvar = getenv("OMP_NUM_THREADS");
  if (envvar != NULL) {
    long value;
    value = strtol(envvar, NULL, 10);
    if ((value != EINVAL) && (value >= 0)) {
      nthreads = (int)value;
    }
  }
  // Build buffers and contexts for computations
  int nchunks_thread = NCHUNKS / nthreads;
  int remaining_chunks = NCHUNKS - nchunks_thread * nthreads;
  blosc2_context **dctx = malloc(nthreads * sizeof(void*));
  DTYPE** chunk = malloc(nthreads * sizeof(void*));
  for (j = 0; j < nthreads; j++) {
    chunk[j] = malloc(CHUNKSIZE * sizeof(DTYPE));
  }

  // Reduce uncompressed dataset
  blosc_set_timestamp(&last);
  ttotal = 1e10;
  compressed_sum = 0;
  for (int n = 0; n < NITER; n++) {
    compressed_sum = 0;
    #pragma omp parallel for private(nchunk) reduction (+:compressed_sum)
    for (j = 0; j < nthreads; j++) {
      dctx[j] = blosc2_create_dctx(dparams);
      for (nchunk = 0; nchunk < nchunks_thread; nchunk++) {
        blosc2_decompress_ctx(dctx[j], schunk->data[j * nchunks_thread + nchunk], (void*)(chunk[j]), isize);
        for (i = 0; i < CHUNKSIZE; i++) {
          compressed_sum += chunk[j][i];
          //compressed_sum += i + (j * nchunks_thread + nchunk) * CHUNKSIZE;
        }
      }
    }
    for (nchunk = NCHUNKS - remaining_chunks; nchunk < NCHUNKS; nchunk++) {
      blosc2_decompress_ctx(dctx[0], schunk->data[nchunk], (void*)(chunk[0]), isize);
      for (i = 0; i < CHUNKSIZE; i++) {
        compressed_sum += chunk[0][i];
        //compressed_sum += i + nchunk * CHUNKSIZE;
      }
    }
    blosc_set_timestamp(&current);
    itotal = blosc_elapsed_secs(last, current);
    if (itotal < ttotal) ttotal = itotal;
  }
  printf("Sum for *compressed* data: %10.0f\n", (double)compressed_sum);
  printf("Sum time for *compressed* data: %.3g s, %.1f MB/s\n",
         ttotal, nbytes / (ttotal * MB));
  //printf("sum, csum: %f, %f\n", sum, compressed_sum);
  if (SYNTHETIC) {
    // for simple precision this is difficult to fulfill
    assert(sum == compressed_sum);
  }
  /* Free resources */
  blosc2_free_schunk(schunk);

  return 0;
}