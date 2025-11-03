#include <omp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TASK_CUTOFF 8192

void merge(uint64_t *arr, uint64_t *aux, int left, int mid, int right) {
  for (int i = left; i <= right; i++) {
    aux[i] = arr[i];
  }

  int i = left, j = mid + 1, k = left;
  while (i <= mid && j <= right) {
    if (aux[i] <= aux[j]) {
      arr[k++] = aux[i++];
    } else {
      arr[k++] = aux[j++];
    }
  }

  while (i <= mid)
    arr[k++] = aux[i++];
  while (j <= right)
    arr[k++] = aux[j++];
}

void parallel_merge_sort(uint64_t *arr, uint64_t *aux, int left, int right) {
  if (left >= right) {
    return;
  }

  int mid = left + (right - left) / 2;

  if (right - left < TASK_CUTOFF) {
    parallel_merge_sort(arr, aux, left, mid);
    parallel_merge_sort(arr, aux, mid + 1, right);
  } else {
#pragma omp task shared(arr, aux)
    parallel_merge_sort(arr, aux, left, mid);

#pragma omp task shared(arr, aux)
    parallel_merge_sort(arr, aux, mid + 1, right);

#pragma omp taskwait
  }

  merge(arr, aux, left, mid, right);
}

uint64_t *read_input(const char *filename, size_t *count) {
  FILE *fp = fopen(filename, "r");
  if (!fp) {
    fprintf(stderr, "Error opening file: %s\n", filename);
    exit(1);
  }

  // Count lines
  size_t n = 0;
  uint64_t temp;
  while (fscanf(fp, "%lu", &temp) == 1) {
    n++;
  }

  // Allocate array
  uint64_t *arr = malloc(n * sizeof(uint64_t));
  if (!arr) {
    fprintf(stderr, "Memory allocation failed\n");
    exit(1);
  }

  // Read data
  rewind(fp);
  for (size_t i = 0; i < n; i++) {
    if (fscanf(fp, "%lu", &arr[i]) != 1) {
      fprintf(stderr, "Error reading input file\n");
      exit(1);
    }
  }

  fclose(fp);
  *count = n;
  return arr;
}

int main(int argc, char *argv[]) {
  if (argc < 3 || argc > 4) {
    fprintf(stderr, "Usage: %s <num_threads> <input_file> [--no-output]\n",
            argv[0]);
    return 1;
  }

  int num_threads = atoi(argv[1]);
  char *filename = argv[2];
  bool print_output = true;

  if (argc == 4 && strcmp(argv[3], "--no-output") == 0) {
    print_output = false;
  }

  size_t count;
  uint64_t *numbers = read_input(filename, &count);

  uint64_t *aux = malloc(count * sizeof(uint64_t));
  if (!aux) {
    fprintf(stderr, "Memory allocation failed\n");
    return 1;
  }

  omp_set_num_threads(num_threads);

  // Time only the sorting (per spec)
  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);

#pragma omp parallel
  {
#pragma omp single
    parallel_merge_sort(numbers, aux, 0, count - 1);
  }

  clock_gettime(CLOCK_MONOTONIC, &end);

  long long time_us = (end.tv_sec - start.tv_sec) * 1000000LL +
                      (end.tv_nsec - start.tv_nsec) / 1000;

  printf("MergeSort: %lld\n", time_us);

  if (print_output) {
    for (size_t i = 0; i < count; i++) {
      printf("%lu\n", numbers[i]);
    }
  }

  free(aux);
  free(numbers);
  return 0;
}
