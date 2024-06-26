/*********************************************************************
  Blosc - Blocked Shuffling and Compression Library

  Roundtrip tests

  Copyright (c) 2021  Blosc Development Team <blosc@blosc.org>
  https://blosc.org
  License: BSD 3-Clause (see LICENSE.txt)

  See LICENSE.txt for details about copyright and rights to use.
**********************************************************************/

#include "test_common.h"
#include "../blosc/shuffle.h"
#include "../blosc/shuffle-generic.h"


/** Roundtrip tests for the generic shuffle/unshuffle. */
static int test_shuffle_roundtrip_generic(size_t type_size, size_t num_elements,
                                          size_t buffer_alignment) {
  size_t buffer_size = type_size * num_elements;

  /* Allocate memory for the test. */
  void* original = blosc_test_malloc(buffer_alignment, buffer_size);
  void* shuffled = blosc_test_malloc(buffer_alignment, buffer_size);
  void* unshuffled = blosc_test_malloc(buffer_alignment, buffer_size);

  /* Fill the input data buffer with random values. */
  blosc_test_fill_seq(original, buffer_size);

  /* Generic shuffle, then generic unshuffle. */
  shuffle_generic((int32_t)type_size, (int32_t)buffer_size, original, shuffled);
  unshuffle_generic((int32_t)type_size, (int32_t)buffer_size, shuffled, unshuffled);

  /* The round-tripped data matches the original data when the
     result of memcmp is 0. */
  int exit_code = memcmp(original, unshuffled, buffer_size) ?
                  EXIT_FAILURE : EXIT_SUCCESS;

  /* Free allocated memory. */
  blosc_test_free(original);
  blosc_test_free(shuffled);
  blosc_test_free(unshuffled);

  return exit_code;
}

/** Required number of arguments to this test, including the executable name. */
#define TEST_ARG_COUNT  4

int main(int argc, char** argv) {
  /*  argv[1]: sizeof(element type)
      argv[2]: number of elements
      argv[3]: buffer alignment
  */

  /*  Verify the correct number of command-line args have been specified. */
  if (TEST_ARG_COUNT != argc) {
    blosc_test_print_bad_argcount_msg(TEST_ARG_COUNT, argc);
    return EXIT_FAILURE;
  }

  /* Parse arguments */
  uint32_t type_size;
  if (!blosc_test_parse_uint32_t(argv[1], &type_size) || (type_size < 1)) {
    blosc_test_print_bad_arg_msg(1);
    return EXIT_FAILURE;
  }

  uint32_t num_elements;
  if (!blosc_test_parse_uint32_t(argv[2], &num_elements) || (num_elements < 1)) {
    blosc_test_print_bad_arg_msg(2);
    return EXIT_FAILURE;
  }

  uint32_t buffer_align_size;
  if (!blosc_test_parse_uint32_t(argv[3], &buffer_align_size)
      || (buffer_align_size & (buffer_align_size - 1))
      || (buffer_align_size < sizeof(void*))) {
    blosc_test_print_bad_arg_msg(3);
    return EXIT_FAILURE;
  }

  /* Run the test. */
  return test_shuffle_roundtrip_generic(type_size, num_elements, buffer_align_size);
}
