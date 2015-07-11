#include <ruby/ruby.h>
#include <ruby/intern.h>
#include <assert.h>
#include <malloc.h>
#include <stddef.h>

#include <stdio.h>
#ifdef HAV_STDLIB_H
#include <stdlib.h>
#endif
#include <string.h>

typedef unsigned int u32;
typedef unsigned char u8;

typedef struct hash_item {
	u32 pos;
	u32 rc : 31;
	u32 big : 1;
#ifndef HAVE_MALLOC_USABLE_SIZE
	u32 item_size;
#endif
	union {
		struct {
			u8 key_size;
			u8 val_size;
			char key[0];
		} small;
		struct {
			u32 key_size;
			u32 val_size;
			char key[0];
		} big;
	} kind;
} hash_item;

#ifdef HAVE_MALLOC_USABLE_SIZE
static inline size_t
item_size(hash_item* item) {
	return malloc_usable_size(item);
}
#else
static inline size_t
item_size(hash_item* item) {
	return item->item_size;
}
#endif

static inline int
item_need_big(u32 key_size, u32 val_size) {
	return key_size > 255 || val_size > 255;
}

static inline u32
item_key_size(hash_item* item) {
	return item->big ? item->kind.big.key_size : item->kind.small.key_size;
}

static inline u32
item_val_size(hash_item* item) {
	return item->big ? item->kind.big.val_size : item->kind.small.val_size;
}

static inline void
item_set_sizes(hash_item* item, u32 key_size, u32 val_size) {
	if (item_need_big(key_size, val_size)) {
		item->big = 1;
		item->kind.big.key_size = key_size;
		item->kind.big.val_size = val_size;
	} else {
		item->big = 0;
		item->kind.small.key_size = key_size;
		item->kind.small.val_size = val_size;
	}
}

static inline void
item_set_val_size(hash_item* item, u32 val_size) {
	assert(val_size <= 255 || item->big == 1);
	if (item->big) {
		item->kind.big.val_size = val_size;
	} else {
		item->kind.small.val_size = val_size;
	}
}

static inline char*
item_key(hash_item* item) {
	return item->big ? item->kind.big.key : item->kind.small.key;
}

static inline char*
item_val(hash_item* item) {
	return item_key(item) + item_key_size(item);
}

static inline u32
item_need_size(u32 key_size, u32 val_size) {
	if (item_need_big(key_size, val_size)) {
		return offsetof(hash_item, kind.big.key) + key_size + val_size;
	} else {
		return offsetof(hash_item, kind.small.key) + key_size + val_size;
	}
}

static inline int
item_compatible(hash_item* item, u32 val_size) {
	u32 key_size, need_size, have_size;
	key_size = item_key_size(item);
	if (item->big != item_need_big(key_size, val_size))
		return 0;
	need_size = item_need_size(key_size, val_size);
	have_size = item_size(item);
	if (need_size > have_size || need_size < have_size/2)
		return 0;
	return 1;
}

typedef struct hash_entry {
	u32 hash;
	u32 next;
	u32 fwd;
	u32 prev;
	hash_item* item;
} hash_entry;

typedef struct hash_table {
	hash_entry* entries;
	u32* buckets;
	u32  size;
	u32  alloced;
	u32  empty;
	u32  first;
	u32  last;
	u32  nbuckets;
} hash_table;

static const u32 end = (u32)0 - 1;

static u32 hash_first(hash_table* tab);
static u32 hash_next(hash_table* tab, u32 pos);
static u32 hash_hash_first(hash_table* tab, u32 hash);
static u32 hash_hash_next(hash_table* tab, u32 hash, u32 pos);
static u32 hash_insert(hash_table* tab, u32 hash);
static void hash_up(hash_table* tab, u32 pos);
static void hash_delete(hash_table* tab, u32 pos);
static void hash_destroy(hash_table* tab);
static size_t hash_memsize(const hash_table* tab) {
	return tab->alloced * sizeof(hash_entry) +
		tab->nbuckets * sizeof(u32);
}

static u32
hash_first(hash_table* tab) {
	return tab->first - 1;
}

static u32
hash_next(hash_table* tab, u32 pos) {
	if (pos == end || tab->alloced < pos) {
		return end;
	}
	return tab->entries[pos].fwd - 1;
}

static u32
hash_hash_first(hash_table* tab, u32 hash) {
	u32 buc, pos;
	if (tab->size == 0) return end;
	buc = hash % tab->nbuckets;
	pos = tab->buckets[buc] - 1;
	while (pos != end && tab->entries[pos].hash != hash) {
		pos = tab->entries[pos].next - 1;
	}
	return pos;
}

static u32
hash_hash_next(hash_table* tab, u32 hash, u32 pos) {
	if (pos == end || tab->size == 0) return end;
	do {
		pos = tab->entries[pos].next - 1;
	} while (pos != end && tab->entries[pos].hash != hash);
	return pos;
}

#if 0
static void
hash_print(hash_table* tab, const char* act, u32 pos) {
	u32 i;
	printf("%s %d size: %d first: %d last: %d\n", act, pos, tab->size, tab->first-1, tab->last-1);
	i = tab->first;
	while(i-1!=end) {
		hash_entry* e = tab->entries + (i-1);
		printf("\tpos: %d prev: %d fwd: %d\n", i-1, e->prev-1, e->fwd-1);
		i = tab->entries[i-1].fwd;
	}
}
#else
#define hash_print(tab, act, pos)
#endif

static inline void
hash_enchain(hash_table* tab, u32 pos) {
	tab->entries[pos].prev = tab->last;
	if (tab->first == 0) {
		tab->first = pos+1;
	} else {
		tab->entries[tab->last-1].fwd = pos+1;
	}
	tab->last = pos+1;
	hash_print(tab, "enchain", pos);
}

static inline void
hash_enchain_first(hash_table* tab, u32 pos) {
	tab->entries[pos].fwd = tab->first;
	if (tab->last == 0) {
		tab->last = pos+1;
	} else {
		tab->entries[tab->first-1].prev = pos+1;
	}
	tab->first = pos+1;
	hash_print(tab, "enchain first", pos);
}

static inline void
hash_unchain(hash_table* tab, u32 pos) {
	if (tab->first == pos+1) {
		tab->first = tab->entries[pos].fwd;
	} else {
		tab->entries[tab->entries[pos].prev-1].fwd = tab->entries[pos].fwd;
	}
	if (tab->last == pos+1) {
		tab->last = tab->entries[pos].prev;
	} else {
		tab->entries[tab->entries[pos].fwd-1].prev = tab->entries[pos].prev;
	}
	tab->entries[pos].fwd = 0;
	tab->entries[pos].prev = 0;
	hash_print(tab, "unchain", pos);
}

static void
hash_up(hash_table* tab, u32 pos) {
	assert(tab->entries[pos].item != NULL);
	if (tab->last == pos+1) return;
	hash_unchain(tab, pos);
	hash_enchain(tab, pos);
}

static void
hash_down(hash_table* tab, u32 pos) {
	assert(tab->entries[pos].item != NULL);
	if (tab->first == pos+1) return;
	hash_unchain(tab, pos);
	hash_enchain_first(tab, pos);
}

static u32
hash_insert(hash_table* tab, u32 hash) {
	u32 i, pos, buc, npos;
	if (tab->size == tab->alloced) {
		u32 new_alloced = tab->alloced ? tab->alloced * 1.5 : 32;
		hash_entry* new_entries = realloc(tab->entries,
				sizeof(hash_entry)*new_alloced);
		if (new_entries == NULL)
			return end;
		tab->entries = new_entries;
		memset(tab->entries + tab->alloced, 0,
				sizeof(hash_entry)*(new_alloced - tab->alloced));
		for (i=tab->alloced; i<new_alloced-1; i++) {
			tab->entries[i].next = i+2;
		}
		tab->empty = tab->alloced+1;
		tab->alloced = new_alloced;
	}
	if (tab->size >= tab->nbuckets * 2) {
		u32 new_nbuckets = tab->nbuckets ? (tab->nbuckets+1)*2-1 : 15;
		u32* new_buckets = calloc(new_nbuckets, sizeof(u32));
		if (new_buckets == NULL)
			return end;
		free(tab->buckets);
		tab->buckets = new_buckets;
		for (i=0; i<tab->alloced; i++) {
			if (tab->entries[i].item == NULL)
				continue;
			buc = tab->entries[i].hash % new_nbuckets;
			npos = tab->buckets[buc];
			tab->entries[i].next = npos;
			tab->buckets[buc] = i+1;
		}
		tab->nbuckets = new_nbuckets;
	}
	buc = hash % tab->nbuckets;
	npos = tab->buckets[buc];
	pos = tab->empty - 1;
	assert(pos != end);
	tab->buckets[buc] = pos + 1;
	tab->empty = tab->entries[pos].next;
	tab->entries[pos].hash = hash;
	tab->entries[pos].item = NULL;
	tab->entries[pos].next = npos;
	tab->entries[pos].fwd = 0;
	hash_enchain(tab, pos);
	tab->size++;
	return pos;
}

static void
hash_delete(hash_table* tab, u32 pos) {
	u32 hash, buc, i, j;
	hash = tab->entries[pos].hash;
	buc = hash % tab->nbuckets;
	i = tab->buckets[buc] - 1;
	j = end;
	while (i != pos && i != end) {
		j = i;
		i = tab->entries[i].next - 1;
	}
	assert(i != end && i == pos);
	if (j == end) {
		tab->buckets[buc] = tab->entries[i].next;
	} else {
		tab->entries[j].next = tab->entries[i].next;
	}
	tab->entries[i].next = tab->empty;
	hash_unchain(tab, i);
	tab->empty = i+1;
	tab->entries[i].hash = 0;
	tab->entries[i].item = NULL;
	tab->size--;
}

static void
hash_destroy(hash_table* tab) {
	free(tab->entries);
	free(tab->buckets);
}

typedef struct inmemory_kv {
	hash_table tab;
	size_t total_size;
} inmemory_kv;

static hash_item* kv_insert(inmemory_kv *kv, const char* key, u32 key_size, const char* val, u32 val_size);
static hash_item* kv_fetch(inmemory_kv *kv, const char* key, u32 key_size);
static void kv_up(inmemory_kv *kv, hash_item* item);
static void kv_delete(inmemory_kv *kv, hash_item* item);
static hash_item* kv_first(inmemory_kv *kv);

typedef void (*kv_each_cb)(hash_item* item, void* arg);
static void kv_each(inmemory_kv *kv, kv_each_cb cb, void* arg);

static void kv_copy_to(inmemory_kv *from, inmemory_kv *to);

#ifdef HAV_RB_MEMHASH
static inline u32
kv_hash(const char* key, u32 key_size) {
	return rb_memhash(key, key_size);
}
#else
static inline u32
kv_hash(const char* key, u32 key_size) {
	u32 a1 = 0xdeadbeef, a2 = 0x71fefeed;
	u32 i;
	for (i = 0; i<key_size; i++) {
		unsigned char k = key[i];
		a1 = (a1 + k) * 5;
		a2 = (a2 ^ k) * 9;
	}
	a1 ^= key_size; a1 *= 5; a2 *= 9;
	return a1 ^ a2;
}
#endif

static hash_item*
kv_insert(inmemory_kv *kv, const char* key, u32 key_size, const char* val, u32 val_size) {
	u32 hash = kv_hash(key, key_size);
	u32 pos;
	hash_item *item, *old_item = NULL;
	pos = hash_hash_first(&kv->tab, hash);
	while (pos != end) {
		item = kv->tab.entries[pos].item;
		if (item_key_size(item) == key_size &&
				memcmp(key, item_key(item), key_size) == 0) {
			break;
		}
		pos = hash_hash_next(&kv->tab, hash, pos);
	}
	if (pos == end) {
		pos = hash_insert(&kv->tab, hash);
		if (pos == end)
			return NULL;
		item = NULL;
	} else {
		hash_up(&kv->tab, pos);
		if (!item_compatible(item, val_size) || item->rc > 0) {
			old_item = item;
			item = NULL;
		}
	}
	if (item == NULL) {
		u32 new_size = item_need_size(key_size, val_size);
#ifndef HAVE_MALLOC_USABLE_SIZE
		new_size = (new_size + 7) & 7;
#endif
		item = malloc(new_size);
		if (item == NULL) {
			if (old_item == NULL) {
				hash_delete(&kv->tab, pos);
			}
			return NULL;
		}
#ifdef HAVE_MALLOC_USABLE_SIZE
		new_size = malloc_usable_size(item);
#else
		item->item_size = new_size;
#endif
		if (old_item != NULL) {
			kv->total_size -= item_size(old_item);
			if (old_item->rc > 0)
				old_item->rc--;
			else
				free(old_item);
		}
		item->rc = 0;
		kv->total_size += new_size;
		item_set_sizes(item, key_size, val_size);
		item->pos = pos;
		memcpy(item_key(item), key, key_size);
	}
	item_set_val_size(item, val_size);
	memcpy(item_val(item), val, val_size);
	kv->tab.entries[pos].item = item;
	return item;
}

static hash_item*
kv_fetch(inmemory_kv *kv, const char* key, u32 key_size) {
	u32 hash = kv_hash(key, key_size);
	u32 pos;
	hash_item* item;
	pos = hash_hash_first(&kv->tab, hash);
	while (pos != end) {
		item = kv->tab.entries[pos].item;
		if (item_key_size(item) == key_size &&
				memcmp(key, item_key(item), key_size) == 0) {
			break;
		}
		pos = hash_hash_next(&kv->tab, hash, pos);
	}
	return pos == end ? NULL : kv->tab.entries[pos].item;
}

static void
kv_up(inmemory_kv *kv, hash_item* item) {
	hash_up(&kv->tab, item->pos);
}

static void
kv_down(inmemory_kv *kv, hash_item* item) {
	hash_down(&kv->tab, item->pos);
}

static void
kv_delete(inmemory_kv *kv, hash_item* item) {
	hash_delete(&kv->tab, item->pos);
	kv->total_size -= item_size(item);
	if (item->rc > 0) {
		item->rc--;
	} else {
		free(item);
	}
}

static hash_item*
kv_first(inmemory_kv *kv) {
	u32 pos = hash_first(&kv->tab);
	if (pos != end) {
		return kv->tab.entries[pos].item;
	}
	return NULL;
}

static void
kv_each(inmemory_kv *kv, kv_each_cb cb, void* arg) {
	u32 pos = hash_first(&kv->tab);
	while (pos != end) {
		cb(kv->tab.entries[pos].item, arg);
		pos = hash_next(&kv->tab, pos);
	}
}

static void
kv_destroy(inmemory_kv *kv) {
	u32 i;
	for (i=0; i<kv->tab.alloced; i++) {
		if (kv->tab.entries[i].item != NULL) {
			hash_item* item = kv->tab.entries[i].item;
			if (item->rc > 0) {
				item->rc--;
			} else {
				free(item);
			}
		}
	}
	hash_destroy(&kv->tab);
}

static void
kv_copy_to(inmemory_kv *from, inmemory_kv *to) {
	kv_destroy(to);
	*to = *from;
	if (to->tab.alloced) {
		u32 i;
		to->tab.entries = malloc(to->tab.alloced*sizeof(hash_entry));
		memcpy(to->tab.entries, from->tab.entries,
				sizeof(hash_entry)*to->tab.alloced);
		to->tab.buckets = malloc(to->tab.nbuckets*sizeof(u32));
		memcpy(to->tab.buckets, from->tab.buckets,
				sizeof(u32)*from->tab.nbuckets);
		for (i=0; i<to->tab.alloced; i++) {
			if (to->tab.entries[i].item != NULL) {
				to->tab.entries[i].item->rc++;
			}
		}
	}
}

static size_t
rb_kv_memsize(const void *p) {
	if (p) {
		const inmemory_kv* kv = p;
		return sizeof(*kv) + kv->total_size + hash_memsize(&kv->tab);
	}
	return 0;
}

static void
rb_kv_destroy(void *p) {
	if (p) {
		inmemory_kv *kv = p;
		kv_destroy(kv);
		free(kv);
	}
}

static const rb_data_type_t InMemoryKV_data_type = {
	"InMemoryKV_C",
	{NULL, rb_kv_destroy, rb_kv_memsize}
};
#define GetKV(value, pointer) \
	TypedData_Get_Struct((value), inmemory_kv, &InMemoryKV_data_type, (pointer))

static VALUE
rb_kv_alloc(VALUE klass) {
	inmemory_kv* kv = calloc(1, sizeof(inmemory_kv));
	return TypedData_Wrap_Struct(klass, &InMemoryKV_data_type, kv);
}

static inline VALUE
item_key_str(hash_item* item) {
	return rb_str_new(item_key(item), item_key_size(item));
}

static inline VALUE
item_val_str(hash_item* item) {
	return rb_str_new(item_val(item), item_val_size(item));
}

static VALUE
rb_kv_get(VALUE self, VALUE vkey) {
	inmemory_kv* kv;
	const char *key;
	size_t size;
	hash_item* item;

	GetKV(self, kv);
	StringValue(vkey);
	key = RSTRING_PTR(vkey);
	size = RSTRING_LEN(vkey);
	item = kv_fetch(kv, key, size);
	if (item == NULL) return Qnil;
	return item_val_str(item);
}

static VALUE
rb_kv_up(VALUE self, VALUE vkey) {
	inmemory_kv* kv;
	const char *key;
	size_t size;
	hash_item* item;

	GetKV(self, kv);
	StringValue(vkey);
	key = RSTRING_PTR(vkey);
	size = RSTRING_LEN(vkey);
	item = kv_fetch(kv, key, size);
	if (item == NULL) return Qnil;
	kv_up(kv, item);
	return item_val_str(item);
}

static VALUE
rb_kv_down(VALUE self, VALUE vkey) {
	inmemory_kv* kv;
	const char *key;
	size_t size;
	hash_item* item;

	GetKV(self, kv);
	StringValue(vkey);
	key = RSTRING_PTR(vkey);
	size = RSTRING_LEN(vkey);
	item = kv_fetch(kv, key, size);
	if (item == NULL) return Qnil;
	kv_down(kv, item);
	return item_val_str(item);
}

static VALUE
rb_kv_include(VALUE self, VALUE vkey) {
	inmemory_kv* kv;
	const char *key;
	size_t size;

	GetKV(self, kv);
	StringValue(vkey);
	key = RSTRING_PTR(vkey);
	size = RSTRING_LEN(vkey);
	return kv_fetch(kv, key, size) ? Qtrue : Qfalse;
}

static VALUE
rb_kv_set(VALUE self, VALUE vkey, VALUE vval) {
	inmemory_kv* kv;
	const char *key, *val;
	size_t ksize, vsize;

	GetKV(self, kv);
	StringValue(vkey);
	StringValue(vval);
	key = RSTRING_PTR(vkey);
	ksize = RSTRING_LEN(vkey);
	val = RSTRING_PTR(vval);
	vsize = RSTRING_LEN(vval);

	if (kv_insert(kv, key, ksize, val, vsize) == NULL) {
		rb_raise(rb_eNoMemError, "could not malloc");
	}

	return vval;
}

static VALUE
rb_kv_del(VALUE self, VALUE vkey) {
	inmemory_kv* kv;
	const char *key;
	size_t size;
	hash_item* item;
	VALUE res;

	GetKV(self, kv);
	StringValue(vkey);
	key = RSTRING_PTR(vkey);
	size = RSTRING_LEN(vkey);
	item = kv_fetch(kv, key, size);
	if (item == NULL) return Qnil;
	res = item_val_str(item);
	kv_delete(kv, item);
	return res;
}

static VALUE
rb_kv_first(VALUE self) {
	inmemory_kv* kv;
	hash_item* item;
	VALUE key, val;

	GetKV(self, kv);
	item = kv_first(kv);
	if (item == NULL) return Qnil;
	key = item_key_str(item);
	val = item_val_str(item);
	return rb_assoc_new(key, val);
}

static VALUE
rb_kv_shift(VALUE self) {
	inmemory_kv* kv;
	hash_item* item;
	VALUE key, val;

	GetKV(self, kv);
	item = kv_first(kv);
	if (item == NULL) return Qnil;
	key = item_key_str(item);
	val = item_val_str(item);
	kv_delete(kv, item);
	return rb_assoc_new(key, val);
}

static VALUE
rb_kv_unshift(VALUE self, VALUE vkey, VALUE vval) {
	inmemory_kv* kv;
	const char *key, *val;
	size_t ksize, vsize;
	hash_item* item;

	GetKV(self, kv);
	StringValue(vkey);
	StringValue(vval);
	key = RSTRING_PTR(vkey);
	ksize = RSTRING_LEN(vkey);
	val = RSTRING_PTR(vval);
	vsize = RSTRING_LEN(vval);

	item = kv_insert(kv, key, ksize, val, vsize);
	if (item == NULL) {
		rb_raise(rb_eNoMemError, "could not malloc");
	}
	kv_down(kv, item);

	return vval;
}

static VALUE
rb_kv_size(VALUE self) {
	inmemory_kv* kv;
	GetKV(self, kv);
	return UINT2NUM(kv->tab.size);
}

static VALUE
rb_kv_empty_p(VALUE self) {
	inmemory_kv* kv;
	GetKV(self, kv);
	return kv->tab.size ? Qfalse : Qtrue;
}

static VALUE
rb_kv_data_size(VALUE self) {
	inmemory_kv* kv;
	GetKV(self, kv);
	return SIZET2NUM(kv->total_size);
}

static VALUE
rb_kv_total_size(VALUE self) {
	inmemory_kv* kv;
	GetKV(self, kv);
	return SIZET2NUM(rb_kv_memsize(kv));
}

static void
keys_i(hash_item* item, void* arg) {
	VALUE ary = (VALUE)arg;
	rb_ary_push(ary, item_key_str(item));
}

static void
vals_i(hash_item* item, void* arg) {
	VALUE ary = (VALUE)arg;
	rb_ary_push(ary, item_val_str(item));
}

static void
pairs_i(hash_item* item, void* arg) {
	VALUE ary = (VALUE)arg;
	VALUE key, val;
	key = item_key_str(item);
	val = item_val_str(item);
	rb_ary_push(ary, rb_assoc_new(key, val));
}

static VALUE
rb_kv_keys(VALUE self) {
	inmemory_kv* kv;
	VALUE res;
	GetKV(self, kv);
	res = rb_ary_new2(kv->tab.size);
	kv_each(kv, keys_i, (void*)res);
	return res;
}

static VALUE
rb_kv_vals(VALUE self) {
	inmemory_kv* kv;
	VALUE res;
	GetKV(self, kv);
	res = rb_ary_new2(kv->tab.size);
	kv_each(kv, vals_i, (void*)res);
	return res;
}

static VALUE
rb_kv_entries(VALUE self) {
	inmemory_kv* kv;
	VALUE res;
	GetKV(self, kv);
	res = rb_ary_new2(kv->tab.size);
	kv_each(kv, pairs_i, (void*)res);
	return res;
}

static void
key_i(hash_item* item, void* _ __attribute__((unused))) {
	rb_yield(item_key_str(item));
}

static void
val_i(hash_item* item, void* _ __attribute__((unused))) {
	rb_yield(item_val_str(item));
}

static void
pair_i(hash_item* item, void* _ __attribute__((unused))) {
	VALUE key, val;
	key = item_key_str(item);
	val = item_val_str(item);
	rb_yield(rb_assoc_new(key, val));
}

static VALUE
rb_kv_each_key(VALUE self) {
	inmemory_kv* kv;
	GetKV(self, kv);
	RETURN_ENUMERATOR(self, 0, 0);
	kv_each(kv, key_i, NULL);
	return self;
}

static VALUE
rb_kv_each_val(VALUE self) {
	inmemory_kv* kv;
	GetKV(self, kv);
	RETURN_ENUMERATOR(self, 0, 0);
	kv_each(kv, val_i, NULL);
	return self;
}

static VALUE
rb_kv_each(VALUE self) {
	inmemory_kv* kv;
	GetKV(self, kv);
	RETURN_ENUMERATOR(self, 0, 0);
	kv_each(kv, pair_i, NULL);
	return self;
}

struct inspect_arg {
	VALUE str, tmp;
};
static void
inspect_i(hash_item* item, void* arg) {
	struct inspect_arg* a = arg;
	VALUE ins;
	rb_str_cat(a->tmp, item_key(item), item_key_size(item));
	ins = rb_inspect(a->tmp);
	rb_str_cat(a->str, " ", 1);
	rb_str_cat(a->str, RSTRING_PTR(ins), RSTRING_LEN(ins));
	rb_str_cat(a->str, "=>", 2);
	rb_str_resize(ins, 0);
	rb_str_resize(a->tmp, 0);
	rb_str_buf_cat(a->tmp, item_val(item), item_val_size(item));
	ins = rb_inspect(a->tmp);
	rb_str_append(a->str, ins);
	rb_str_resize(ins, 0);
	rb_str_resize(a->tmp, 0);
}

static VALUE
rb_kv_inspect(VALUE self) {
	struct inspect_arg ins;
	inmemory_kv* kv;
	GetKV(self, kv);
	ins.str = rb_str_buf_new2("<");
	rb_str_append(ins.str, rb_class_name(CLASS_OF(self)));
	if (kv->tab.size != 0) {
		ins.tmp = rb_str_buf_new(0);
		kv_each(kv, inspect_i, &ins);
	}
	rb_str_buf_cat2(ins.str, ">");
	return ins.str;
}

static VALUE
rb_kv_init_copy(VALUE self, VALUE orig) {
	inmemory_kv *origin, *new;
	GetKV(self, new);
	GetKV(orig, origin);
	kv_copy_to(origin, new);
	return self;
}

static VALUE
rb_kv_clear(VALUE self) {
	inmemory_kv* kv;
	GetKV(self, kv);
	kv_destroy(kv);
	memset(kv, 0, sizeof(*kv));
	return self;
}

void
Init_inmemory_kv() {
	VALUE mod_inmemory_kv, cls_str2str;
	mod_inmemory_kv = rb_define_module("InMemoryKV");
	cls_str2str = rb_define_class_under(mod_inmemory_kv, "Str2Str", rb_cObject);
	rb_define_alloc_func(cls_str2str, rb_kv_alloc);
	rb_define_method(cls_str2str, "[]", rb_kv_get, 1);
	rb_define_method(cls_str2str, "up", rb_kv_up, 1);
	rb_define_method(cls_str2str, "down", rb_kv_down, 1);
	rb_define_method(cls_str2str, "[]=", rb_kv_set, 2);
	rb_define_method(cls_str2str, "unshift", rb_kv_unshift, 2);
	rb_define_method(cls_str2str, "delete", rb_kv_del, 1);
	rb_define_method(cls_str2str, "empty?", rb_kv_empty_p, 0);
	rb_define_method(cls_str2str, "size", rb_kv_size, 0);
	rb_define_method(cls_str2str, "count", rb_kv_size, 0);
	rb_define_method(cls_str2str, "data_size", rb_kv_data_size, 0);
	rb_define_method(cls_str2str, "total_size", rb_kv_total_size, 0);
	rb_define_method(cls_str2str, "include?", rb_kv_include, 1);
	rb_define_method(cls_str2str, "has_key?", rb_kv_include, 1);
	rb_define_method(cls_str2str, "first", rb_kv_first, 0);
	rb_define_method(cls_str2str, "shift", rb_kv_shift, 0);
	rb_define_method(cls_str2str, "keys", rb_kv_keys, 0);
	rb_define_method(cls_str2str, "values", rb_kv_vals, 0);
	rb_define_method(cls_str2str, "entries", rb_kv_entries, 0);
	rb_define_method(cls_str2str, "each_key", rb_kv_each_key, 0);
	rb_define_method(cls_str2str, "each_value", rb_kv_each_val, 0);
	rb_define_method(cls_str2str, "each_pair", rb_kv_each, 0);
	rb_define_method(cls_str2str, "each", rb_kv_each, 0);
	rb_define_method(cls_str2str, "inspect", rb_kv_inspect, 0);
	rb_define_method(cls_str2str, "initialize_copy", rb_kv_init_copy, 1);
	rb_define_method(cls_str2str, "clear", rb_kv_clear, 0);
	rb_include_module(cls_str2str, rb_mEnumerable);
}
