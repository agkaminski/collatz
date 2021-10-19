/* Collazt counter-example finder
 * Copyright Aleksander Kaminski 2021
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#define BNUM_LEN 8
#define PRINTLEN 100
#define SILENT 1
#define THRNO 8

typedef struct { uint32_t num[BNUM_LEN]; } bnum_t;
unsigned char lut[BNUM_LEN * 32][PRINTLEN];
pthread_mutex_t printmutex = PTHREAD_MUTEX_INITIALIZER;
bnum_t startpoint = { { 0, 0, 2 } };

uint32_t adc(uint32_t a, uint32_t b, int *carry)
{
	uint32_t res = a + b + *carry;
	*carry = (res < a || res < b) ? 1 : 0;
	return res;
}


int add(bnum_t *s, bnum_t * const a, bnum_t * const b, int cin)
{
	size_t i;
	int carry = cin;

	for (i = 0; i < BNUM_LEN; ++i)
		s->num[i] = adc(a->num[i], b->num[i], &carry);

	return carry;
}


int lsr(bnum_t *a)
{
	size_t i;
	int carry = a->num[0] & 1;

	for (i = 0; i < BNUM_LEN - 1; ++i)
		a->num[i] = (a->num[i] >> 1) | ((a->num[i + 1] & 1) << 31);

	a->num[BNUM_LEN - 1] >>= 1;

	return carry;
}


int lsl(bnum_t *a)
{
	size_t i;
	int carry = a->num[BNUM_LEN - 1] >> 31;

	for (i = BNUM_LEN - 1; i > 0; --i)
		a->num[i] = (a->num[i] << 1) | ((a->num[i - 1] & (1UL << 31)) >> 31);

	a->num[0] <<= 1;

	return carry;
}


int next(bnum_t *n)
{
	bnum_t a = *n, b;

	if (n->num[0] & 1) {
		if (lsl(n) || add(&b, n, &a, 1))
			return -1;

		*n = b;
	}

	lsr(n);

	return 0;
}


int checkpow2(bnum_t * const n)
{
	size_t i, j;
	int ones = 0, t;

	for (i = 0; i < BNUM_LEN; ++i) {
		if (n->num[i] == 0)
			continue;

		for (t = 0, j = 0; j < 32; ++j) {
			if (n->num[i] & (1UL << j)) {
				if (t)
					return 0;
				++t;
			}
		}

		if (t & ones)
			return 0;

		++ones;
	}

	return ones;
}


int compare(bnum_t * const a, bnum_t * const b)
{
	int i;

	for (i = BNUM_LEN; i >= 0; --i) {
		if (a->num[i] < b->num[i])
			return 1;
	}

	return 0;
}


void add_digs(unsigned char a[PRINTLEN], const unsigned char b[PRINTLEN])
{
	unsigned char carry = 0;
	int i;

	for (i = 0; i < PRINTLEN; ++i) {
		a[i] = a[i] + b[i] + carry;
		carry = 0;
		if (a[i] > 9) {
			carry = 1;
			a[i] -= 10;
		}
	}
}


void print(bnum_t * const n)
{
	char str[PRINTLEN + 1], *toprint;
	unsigned char digs[PRINTLEN] = { 0 };
	int i, j;

	for (i = 0; i < BNUM_LEN; ++i) {
		for (j = 0; j < 32; ++j) {
			if (n->num[i] & (1UL << j))
				add_digs(digs, lut[(32 * i) + j]);
		}
	}

	for (i = 0; i < PRINTLEN; ++i)
		str[PRINTLEN - 1 - i] = '0' + digs[i];

	str[PRINTLEN] = '\0';
/*
	for (i = 0; i < BNUM_LEN; ++i)
		printf("%d ", n->num[i]);
	printf("\n");
*/
	for (toprint = str; *toprint == '0'; ++toprint)
		;

	printf("%s\n", toprint);
}


void *thread(void *arg)
{
	bnum_t start = startpoint, inc = { { 0 } }, curr;
	int threadno = (int)(uintptr_t)arg, spoints = 0;
	time_t prev = time(NULL), now;

	inc.num[0] = (THRNO + threadno) * 2 + 1;

	while (1) {
		if (SILENT) {
			now = time(NULL);
			if (now - prev > 5) {
				prev = now;
				pthread_mutex_lock(&printmutex);
				printf("thread %d: %d sp/s. Current starting point:", threadno, spoints / 5);
				print(&start);
				pthread_mutex_unlock(&printmutex);
				spoints = 0;
			}
		}

		if (!SILENT) {
			pthread_mutex_lock(&printmutex);
			printf("thread %d: New starting point:\n", threadno);
			print(&start);
			pthread_mutex_unlock(&printmutex);
		}
		curr = start;
		++spoints;

		while (1) {
			if (next(&curr)) {
				pthread_mutex_lock(&printmutex);
				printf("thread %d: overflow!\n", threadno);
				pthread_mutex_unlock(&printmutex);
				exit(1);
			}

			/* Assume that everything below original starting point
			 * does not give cycle */
			if (compare(&curr, &start)) {
				if (!SILENT) {
					pthread_mutex_lock(&printmutex);
					printf("thread %d: fail, curr below startpoint\n", threadno);
					pthread_mutex_unlock(&printmutex);
				}
				break;
			}

			if (checkpow2(&curr)) {
				if (!SILENT) {
					pthread_mutex_lock(&printmutex);
					printf("thread %d: fail, found pow2\n", threadno);
					pthread_mutex_unlock(&printmutex);
				}
				break;
			}

			if (memcmp(&start, &curr, sizeof(start)) == 0) {
				pthread_mutex_lock(&printmutex);
				printf("thread %d: FOUND NEW LOOP!\n", threadno);
				printf("thread %d: curr: ", threadno);
				print(&curr);
				printf("thread %d: start: ", threadno);
				print(&start);
				pthread_mutex_unlock(&printmutex);
				exit(0);
			}
		}

		if (add(&start, &start, &inc, 0)) {
			pthread_mutex_lock(&printmutex);
			printf("thread %d: overflow!\n", threadno);
			pthread_mutex_unlock(&printmutex);
			exit(1);
		}
	}
}


int main(int argc, char *argv[])
{
	int i;
	pthread_t threads[THRNO - 1];

	lut[0][0] = 1;

	for (i = 1; i < BNUM_LEN * 32 - 1; ++i) {
		memcpy(lut[i], lut[i - 1], sizeof(lut[i]));
		add_digs(lut[i], lut[i - 1]);
	}

	if (argc != 1) {
		for (i = 0; i < BNUM_LEN && i < argc - 1; ++i)
			startpoint.num[i] = strtoul(argv[i + 1], NULL, 10);
	}

	for (i = 0; i < THRNO - 1; ++i)
		pthread_create(&threads[i], NULL, thread, (void *)(uintptr_t)(i + 1));

	thread((void *)THRNO);
}
