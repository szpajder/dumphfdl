/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <time.h>                           // time_t
#include <libacars/hash.h>                  // la_hash_*
#include "util.h"                           // NEW, debug_print
#include "cache.h"

struct cache_entry {
	time_t created_time;
	void (*data_destroy)(void *data);
	void *data;
};

struct cache {
	struct cache_vtable const *vtable;
	la_hash *table;
	time_t last_expiration_time;
	uint32_t ttl;
	uint32_t expiration_interval;
};

/******************************
 * Forward declarations
 ******************************/

static bool is_cache_entry_expired(void const *key, void const *value, void *ctx);
static void cache_entry_destroy(void *entry);

/******************************
 * Public methods
 ******************************/

cache *cache_create(struct cache_vtable const *vtable, uint32_t ttl,
		uint32_t expiration_interval) {
	ASSERT(vtable);

	NEW(struct cache, cache);
	cache->vtable = vtable;
	cache->ttl = ttl;
	cache->expiration_interval = expiration_interval;
	cache->table = la_hash_new(vtable->cache_key_hash, vtable->cache_key_compare,
			vtable->cache_key_destroy, cache_entry_destroy);
	cache->last_expiration_time = time(NULL);
	return cache;
}

void cache_entry_create(cache const *c, void *key, void *value, time_t created_time) {
	ASSERT(c);
	ASSERT(key);
	// NULL 'value' ptr is allowed

	NEW(struct cache_entry, entry);
	entry->data = value;
	entry->created_time = created_time;
	entry->data_destroy = c->vtable->cache_entry_data_destroy;
	la_hash_insert(c->table, key, entry);
}

bool cache_entry_delete(cache const *c, void *key) {
	ASSERT(c);
	ASSERT(key);

	return la_hash_remove(c->table, key);
}

void *cache_entry_lookup(cache *c, void const *key) {
	ASSERT(c);
	ASSERT(key);

	// Periodic cache expiration
	time_t now = time(NULL);
	if(c->last_expiration_time + c->expiration_interval <= now) {
		int expired_cnt = cache_expire(c, now);
//		AC_CACHE_ENTRY_COUNT_ADD(-expired_cnt);
		debug_print(D_CACHE, "last_gc: %ld, now: %ld, expired %d cache entries\n",
				c->last_expiration_time, now, expired_cnt);
		c->last_expiration_time = now;
	}

	struct cache_entry *e = la_hash_lookup(c->table, key);
	if(e == NULL) {
		return NULL;
	} else if(e->created_time + c->ttl < now) {
		debug_print(D_CACHE, "key %p: entry expired\n", key);
		return NULL;
	}
	return e->data;
}

int32_t cache_expire(cache *c, time_t current_timestamp) {
	ASSERT(c);

	time_t min_created_time = current_timestamp - c->ttl;
	return la_hash_foreach_remove(c->table, is_cache_entry_expired, &min_created_time);
}

void cache_destroy(cache *c) {
	if(c != NULL) {
		la_hash_destroy(c->table);
		XFREE(c);
	}
}

/****************************************
 * Private variables and methods
 ****************************************/

// Callback for la_hash_foreach_remove
// Used to expire old entries from the cache
static bool is_cache_entry_expired(void const *key, void const *value, void *ctx) {
	UNUSED(key);

	struct cache_entry const *entry = value;
	time_t min_created_time = *(time_t *)ctx;
	return (entry->created_time <= min_created_time);
}

static void cache_entry_destroy(void *entry) {
	if(entry != NULL) {
		struct cache_entry *e = entry;
		if(e->data_destroy) {
			e->data_destroy(e->data);
		}
		XFREE(e);
	}
}
