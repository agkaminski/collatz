/* Collazt counter-example finder
 * Copyright Aleksander Kaminski 2021
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#include "config.h"

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
	int carry = cin;

	for (size_t i = 0; i < BNUM_LEN; ++i)
		s->num[i] = adc(a->num[i], b->num[i], &carry);

	return carry;
}


int addlsl(bnum_t *s, bnum_t * const a, bnum_t * const b, int cin)
{
	int lslcarry, carry = cin;

	for (size_t i = 0; i < BNUM_LEN; ++i) {
		lslcarry = !!(b->num[i] & (1UL << 31));
		s->num[i] = adc(a->num[i], b->num[i] << 1, &carry);
		carry += lslcarry;
	}

	return carry;
}


int lsr(bnum_t *a)
{
	int carry = a->num[0] & 1;

	for (size_t i = 0; i < BNUM_LEN - 1; ++i)
		a->num[i] = (a->num[i] >> 1) | ((a->num[i + 1] & 1) << 31);

	a->num[BNUM_LEN - 1] >>= 1;

	return carry;
}


int next(bnum_t *n)
{
	if (n->num[0] & 1) {
		if (addlsl(n, n, n, 1))
			return -1;
	}

	lsr(n);

	return 0;
}


int checkpow2(bnum_t * const n)
{
	int ones = 0;
	uint32_t num;

	for (size_t i = 0; i < BNUM_LEN; ++i) {
		num = n->num[i];

		while (num) {
			num &= num - 1;
			if (ones++)
				return 0;
		}
	}

	return ones;
}


int compare(bnum_t * const a, bnum_t * const b)
{
	for (int i = BNUM_LEN - 1; i >= 0; --i) {
		if (a->num[i] < b->num[i])
			return 1;
	}

	return 0;
}


void add_digs(unsigned char a[PRINTLEN], const unsigned char b[PRINTLEN])
{
	unsigned char carry = 0;

	for (int i = 0; i < PRINTLEN; ++i) {
		a[i] = a[i] + b[i] + carry;
		carry = 0;
		if (a[i] > 9) {
			carry = 1;
			a[i] -= 10;
		}
	}
}


const char *bnum2str(bnum_t * const n)
{
	static char str[PRINTLEN + 1];
	unsigned char digs[PRINTLEN] = { 0 };
	int lastdig = 0;

	for (int i = 0; i < BNUM_LEN; ++i) {
		for (int j = 0; j < 32; ++j) {
			if (n->num[i] & (1UL << j))
				add_digs(digs, lut[(32 * i) + j]);
		}
	}

	for (int i = 0; i < PRINTLEN; ++i) {
		str[PRINTLEN - 1 - i] = '0' + digs[i];
		if (digs[i])
			lastdig = i;
	}

	str[PRINTLEN] = '\0';
/*
	for (i = 0; i < BNUM_LEN; ++i)
		printf("%d ", n->num[i]);
	printf("\n");
*/

	return &str[PRINTLEN - 1 - lastdig];
}


void *thread(void *arg)
{
	bnum_t start = startpoint, inc = { { 0 } }, curr;
	int threadno = (int)(uintptr_t)arg, spoints = 0;
	time_t prev = time(NULL) + threadno, now;
	const char *str;

	inc.num[0] = (THRNO + threadno) * 2;
	start.num[0] |= 1;

	while (1) {
		if (SILENT) {
			now = time(NULL);
			if (now - prev > LOGTIME) {
				prev = now;
				str = bnum2str(&start);
				pthread_mutex_lock(&printmutex);
				printf("t%d: %d ksp/s. curr: %s\n", threadno, (spoints / LOGTIME) / 1000, str);
				pthread_mutex_unlock(&printmutex);
				spoints = 0;
			}
		}

		if (!SILENT) {
			str = bnum2str(&start);
			pthread_mutex_lock(&printmutex);
			printf("thread %d: New starting point: %s\n", threadno, str);
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
				printf("thread %d: FOUND LOOP!\n", threadno);
				str = bnum2str(&curr);
				printf("thread %d: curr: %s\n", threadno, str);
				str = bnum2str(&start);
				printf("thread %d: start: %s\n", threadno, str);
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
