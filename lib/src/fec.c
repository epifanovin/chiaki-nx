// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <chiaki/fec.h>

#include <jerasure.h>
#include <cauchy.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct chiaki_fec_matrix_cache_entry_t
{
	unsigned int k;
	unsigned int m;
	uint64_t age;
	int *matrix;
} ChiakiFecMatrixCacheEntry;

#define CHIAKI_FEC_MATRIX_CACHE_SIZE 8

static ChiakiFecMatrixCacheEntry g_fec_matrix_cache[CHIAKI_FEC_MATRIX_CACHE_SIZE];
static uint64_t g_fec_matrix_cache_age = 1;

static int *get_cached_matrix(unsigned int k, unsigned int m)
{
	for(size_t i = 0; i < CHIAKI_FEC_MATRIX_CACHE_SIZE; i++)
	{
		ChiakiFecMatrixCacheEntry *entry = &g_fec_matrix_cache[i];
		if(entry->matrix && entry->k == k && entry->m == m)
		{
			entry->age = g_fec_matrix_cache_age++;
			return entry->matrix;
		}
	}

	int *matrix = cauchy_original_coding_matrix(k, m, CHIAKI_FEC_WORDSIZE);
	if(!matrix)
		return NULL;

	size_t replace_index = 0;
	uint64_t oldest_age = UINT64_MAX;
	for(size_t i = 0; i < CHIAKI_FEC_MATRIX_CACHE_SIZE; i++)
	{
		ChiakiFecMatrixCacheEntry *entry = &g_fec_matrix_cache[i];
		if(!entry->matrix)
		{
			replace_index = i;
			oldest_age = 0;
			break;
		}
		if(entry->age < oldest_age)
		{
			replace_index = i;
			oldest_age = entry->age;
		}
	}

	ChiakiFecMatrixCacheEntry *replace = &g_fec_matrix_cache[replace_index];
	if(replace->matrix)
		free(replace->matrix);
	replace->matrix = matrix;
	replace->k = k;
	replace->m = m;
	replace->age = g_fec_matrix_cache_age++;
	return matrix;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_fec_decode(uint8_t *frame_buf, size_t unit_size, size_t stride, unsigned int k, unsigned int m, const unsigned int *erasures, size_t erasures_count)
{
	if(stride < unit_size)
		return CHIAKI_ERR_INVALID_DATA;
	int *matrix = get_cached_matrix(k, m);
	if(!matrix)
		return CHIAKI_ERR_MEMORY;

	ChiakiErrorCode err = CHIAKI_ERR_SUCCESS;

	int *jerasures = calloc(erasures_count + 1, sizeof(int));
	if(!jerasures)
	{
		err = CHIAKI_ERR_MEMORY;
		goto error_matrix;
	}
	memcpy(jerasures, erasures, erasures_count * sizeof(int));
	jerasures[erasures_count] = -1;

	uint8_t **data_ptrs = calloc(k, sizeof(uint8_t *));
	if(!data_ptrs)
	{
		err = CHIAKI_ERR_MEMORY;
		goto error_jerasures;
	}

	uint8_t **coding_ptrs = calloc(m, sizeof(uint8_t *));
	if(!coding_ptrs)
	{
		err = CHIAKI_ERR_MEMORY;
		goto error_data_ptrs;
	}

	for(size_t i=0; i<k+m; i++)
	{
		uint8_t *buf_ptr = frame_buf + stride * i;
		if(i < k)
			data_ptrs[i] = buf_ptr;
		else
			coding_ptrs[i - k] = buf_ptr;
	}

	int res = jerasure_matrix_decode(k, m, CHIAKI_FEC_WORDSIZE, matrix, 0, jerasures,
									 (char **)data_ptrs, (char **)coding_ptrs, unit_size);

	if(res < 0)
		err = CHIAKI_ERR_FEC_FAILED;
	else
		err = CHIAKI_ERR_SUCCESS;

	free(coding_ptrs);
error_data_ptrs:
	free(data_ptrs);
error_jerasures:
	free(jerasures);
error_matrix:
	return err;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_fec_encode(uint8_t *frame_buf, size_t unit_size, size_t stride, unsigned int k, unsigned int m)
{
	if(stride < unit_size)
		return CHIAKI_ERR_INVALID_DATA;
	int *matrix = get_cached_matrix(k, m);
	if(!matrix)
		return CHIAKI_ERR_MEMORY;

	ChiakiErrorCode err = CHIAKI_ERR_SUCCESS;

	uint8_t **data_ptrs = calloc(k, sizeof(uint8_t *));
	if(!data_ptrs)
	{
		err = CHIAKI_ERR_MEMORY;
		goto error_matrix;
	}

	uint8_t **coding_ptrs = calloc(m, sizeof(uint8_t *));
	if(!coding_ptrs)
	{
		err = CHIAKI_ERR_MEMORY;
		goto error_data_ptrs;
	}

	for(size_t i=0; i<m; i++)
	{
		coding_ptrs[i] = calloc(unit_size, sizeof(uint8_t));
		if(!coding_ptrs[i])
		{
			for(size_t j=0; j<i; j++)
				free(coding_ptrs[j]);
			goto error_coding_ptrs;
		}
	}

	for(size_t i=0; i<k; i++)
	{
		uint8_t *buf_ptr = frame_buf + stride * i;
		data_ptrs[i] = buf_ptr;
	}

	jerasure_matrix_encode(k, m, CHIAKI_FEC_WORDSIZE, matrix,
							(char **)data_ptrs, (char **)coding_ptrs, unit_size);

	for(int i=0; i<m; i++)
		memcpy(frame_buf + k * unit_size + i * unit_size, coding_ptrs[i], unit_size);

	for(int i=0; i<m; i++)
		free(coding_ptrs[i]);
error_coding_ptrs:
	free(coding_ptrs);
error_data_ptrs:
	free(data_ptrs);
error_matrix:
	return err;
}
