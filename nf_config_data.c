struct nfcd_ConfigData;

enum {
	NFCD_TYPE_NULL, NFCD_TYPE_FALSE, NFCD_TYPE_TRUE, NFCD_TYPE_NUMBER, NFCD_TYPE_STRING,
	NFCD_TYPE_ARRAY, NFCD_TYPE_OBJECT
};

#define NFCD_TYPE_MASK (0x7)
#define NFCD_TYPE_BITS (3)

typedef int nfcd_loc;
typedef void * (*nfcd_realloc) (void *ud, void *ptr, int osize, int nsize, const char *file, int line);

struct nfcd_ConfigData *nfcd_make(nfcd_realloc realloc, void *ud, int config_size, int stringtable_size);

nfcd_loc nfcd_root(struct nfcd_ConfigData *cd);
int nfcd_type(struct nfcd_ConfigData *cd, nfcd_loc loc);
double nfcd_to_number(struct nfcd_ConfigData *cd, nfcd_loc loc);
const char *nfcd_to_string(struct nfcd_ConfigData *cd, nfcd_loc loc);

int nfcd_array_size(struct nfcd_ConfigData *cd, nfcd_loc arr);
nfcd_loc nfcd_array_item(struct nfcd_ConfigData *cd, nfcd_loc arr, int i);

int nfcd_object_size(struct nfcd_ConfigData *cd, nfcd_loc loc);
nfcd_loc nfcd_object_key(struct nfcd_ConfigData *cd, int i);
nfcd_loc nfcd_object_value(struct nfcd_ConfigData *cd, int i);
nfcd_loc nfcd_object_lookup(struct nfcd_ConfigData *cd, const char *key);

nfcd_loc nfcd_null();
nfcd_loc nfcd_false();
nfcd_loc nfcd_true();
nfcd_loc nfcd_add_number(struct nfcd_ConfigData **cd, double n);
nfcd_loc nfcd_add_string(struct nfcd_ConfigData **cd, const char *s);
nfcd_loc nfcd_add_array(struct nfcd_ConfigData **cd, int size);
nfcd_loc nfcd_add_object(struct nfcd_ConfigData **cd, int size);
void nfcd_set_root(struct nfcd_ConfigData *cd, nfcd_loc root);

void nfcd_push(struct nfcd_ConfigData **cd, nfcd_loc array, nfcd_loc item);
void nfcd_set_object_value(struct nfcd_ConfigData **cd, nfcd_loc object, const char *key, nfcd_loc value);

// IMPLEMENTATION

#include <memory.h>
#include <stdlib.h>
#include <assert.h>

struct nfst_StringTable;
void nfst_init(struct nfst_StringTable *st, int bytes, int average_string_size);
int nfst_allocated_bytes(struct nfst_StringTable *st);
void nfst_grow(struct nfst_StringTable *st, int bytes);
int nfst_to_symbol(struct nfst_StringTable *st, const char *s);
const char *nfst_to_string(struct nfst_StringTable *, int symbol);

// nfcd_loc encodes type and offset into data
// array:  [size allocated loc...]
// object: [size allocated keys... values...]
// dynamic arrays and objects grow by chaining, nfcd_loc to next section of array
// each part o fthe chain doubls in size

struct nfcd_ConfigData
{
	int allocated_bytes;
	int used_bytes;
	struct nfst_StringTable *string_table;
	nfcd_loc root;
	nfcd_realloc realloc;
	void *realloc_user_data;
};

struct nfcd_Block
{
	int allocated_size;
	int size;
	nfcd_loc next_block;
};

struct nfcd_ObjectItem
{
	nfcd_loc key;
	nfcd_loc value;
};

#define LOC_OFFSET(loc)			((loc) >> NFCD_TYPE_BITS)
#define LOC_TYPE(loc)			((loc) & NFCD_TYPE_MASK)
#define MAKE_LOC(type, offset)	((type) | (offset) << NFCD_TYPE_BITS)

static nfcd_loc write(struct nfcd_ConfigData **cdp, int type, void *p, int count, int zeroes);

static nfcd_loc write(struct nfcd_ConfigData **cdp, int type, void *p, int count, int zeroes)
{
	int total = count + zeroes;
	struct nfcd_ConfigData *cd = *cdp;
	while (cd->used_bytes + total > cd->allocated_bytes) {
		int new_size = cd->allocated_bytes*2;
		cd = cd->realloc(cd->realloc_user_data, cd, cd->allocated_bytes, new_size,
			__FILE__, __LINE__);
		cd->allocated_bytes = new_size;
		*cdp = cd;
	}
	nfcd_loc loc = MAKE_LOC(type, cd->used_bytes);
	memcpy((char *)cd + cd->used_bytes, p, count);
	cd->used_bytes += count;
	memset((char *)cd + cd->used_bytes, 0, zeroes);
	cd->used_bytes += zeroes;
	return loc;
}

struct nfcd_ConfigData *nfcd_make(nfcd_realloc realloc, void *ud, int config_size, int stringtable_size)
{
	if (!config_size)
		config_size = 8*1024;
	if (!stringtable_size)
		stringtable_size = 8*1024;

	struct nfcd_ConfigData *cd = realloc(ud, NULL, 0, config_size, __FILE__, __LINE__);
	struct nfst_StringTable *st = realloc(ud, NULL, 0, stringtable_size, __FILE__, __LINE__);

	cd->allocated_bytes = config_size;
	cd->used_bytes = sizeof(*cd);
	cd->string_table = st;
	cd->root = NFCD_TYPE_NULL;
	cd->realloc = realloc;
	cd->realloc_user_data = ud;

	nfst_init(st, stringtable_size, 15);

	return cd;
}

nfcd_loc nfcd_root(struct nfcd_ConfigData *cd)
{
	return cd->root;
}

int nfcd_type(struct nfcd_ConfigData *cd, nfcd_loc loc)
{
	return LOC_TYPE(loc);
}

double nfcd_to_number(struct nfcd_ConfigData *cd, nfcd_loc loc)
{
	return *(double *)((char *)cd + LOC_OFFSET(loc));
}

const char *nfcd_to_string(struct nfcd_ConfigData *cd, nfcd_loc loc)
{
	return nfst_to_string(cd->string_table, LOC_OFFSET(loc));
}

int nfcd_array_size(struct nfcd_ConfigData *cd, nfcd_loc array)
{
	struct nfcd_Block *arr = (struct nfcd_Block *)((char *)cd + LOC_OFFSET(array));
	int sz = 0;
	sz += arr->size;
	while (arr->next_block) {
		arr = (struct nfcd_Block *)((char *)cd + LOC_OFFSET(arr->next_block));
		sz += arr->size;
	}
	return sz;
}

nfcd_loc nfcd_array_item(struct nfcd_ConfigData *cd, nfcd_loc array, int i)
{
	assert(i >= 0);
	struct nfcd_Block *arr = (struct nfcd_Block *)((char *)cd + LOC_OFFSET(array));
	while (arr->next_block && i >= arr->size) {
		i -= arr->size;
		arr = (struct nfcd_Block *)((char *)cd + LOC_OFFSET(arr->next_block));
	}
	if (i >= arr->size)
		return nfcd_null();
	nfcd_loc *items = (nfcd_loc *)(arr + 1);
	return items[i];
}

nfcd_loc nfcd_null()
{
	return MAKE_LOC(NFCD_TYPE_NULL, 0);
}

nfcd_loc nfcd_false()
{
	return MAKE_LOC(NFCD_TYPE_FALSE, 0);
}

nfcd_loc nfcd_true()
{
	return MAKE_LOC(NFCD_TYPE_TRUE, 0);
}

nfcd_loc nfcd_add_number(struct nfcd_ConfigData **cd, double n)
{
	return write(cd, NFCD_TYPE_NUMBER, &n, sizeof(n), 0);
}

nfcd_loc nfcd_add_string(struct nfcd_ConfigData **cdp, const char *s)
{
	struct nfcd_ConfigData *cd = *cdp;
	struct nfst_StringTable *st = cd->string_table;
	int sym = nfst_to_symbol(st, s);
	while (sym < 0) {
		int old_size = nfst_allocated_bytes(st);
		int new_size = old_size * 2;
		st = cd->realloc(cd->realloc_user_data,
			st, old_size, new_size, __FILE__, __LINE__);
		nfst_grow(st, new_size);
		sym = nfst_to_symbol(st, s);
	}

	return MAKE_LOC(NFCD_TYPE_STRING, sym);
}

nfcd_loc nfcd_add_array(struct nfcd_ConfigData **cdp, int allocated_size)
{
	struct nfcd_Block a = {0};
	a.allocated_size = allocated_size;
	return write(cdp, NFCD_TYPE_ARRAY, &a, sizeof(a), allocated_size * sizeof(nfcd_loc));
}

nfcd_loc nfcd_add_object(struct nfcd_ConfigData **cdp, int allocated_size)
{
	struct nfcd_Block a = {0};
	a.allocated_size = allocated_size;
	return write(cdp, NFCD_TYPE_OBJECT, &a, sizeof(a), allocated_size * sizeof(struct nfcd_ObjectItem));
}

void nfcd_set_root(struct nfcd_ConfigData *cd, nfcd_loc loc)
{
	cd->root = loc;
}

void nfcd_push(struct nfcd_ConfigData **cdp, nfcd_loc array, nfcd_loc item)
{
	struct nfcd_Block *arr = (struct nfcd_Block *)((char *)*cdp + LOC_OFFSET(array));
	while (arr->size == arr->allocated_size) {
		if (arr->next_block == 0)
			arr->next_block = nfcd_add_array(cdp, arr->allocated_size*2);
		arr = (struct nfcd_Block *)((char *)*cdp + LOC_OFFSET(arr->next_block));
	}
	nfcd_loc *items = (nfcd_loc *)(arr + 1);
	items[arr->size] = item;
	++arr->size;
}

void nfcd_set_object_value(struct nfcd_ConfigData **cdp, nfcd_loc object, const char *key_str, nfcd_loc value)
{
	nfcd_loc key = nfcd_add_string(cdp, key_str);

	struct nfcd_Block *block = (struct nfcd_Block *)((char *)*cdp + LOC_OFFSET(object));
	while (1) {
		struct nfcd_ObjectItem *items = (struct nfcd_ObjectItem *)(block + 1);
		for (int i=0; i<block->size; ++i) {
			if (items[i].key == key) {
				items[i].value = value;
				return;
			}
		}
		if (block->size < block->allocated_size)
			break;
		if (block->next_block == 0)
			block->next_block = nfcd_add_object(cdp, block->allocated_size*2);
		block = (struct nfcd_Block *)((char *)*cdp + LOC_OFFSET(block->next_block));
	}

	struct nfcd_ObjectItem *items = (struct nfcd_ObjectItem *)(block + 1);
	items[block->size].key = key;
	items[block->size].value = value;
	++block->size;
}

#ifdef NFCD_UNIT_TEST

	#include <stdlib.h>
	#include <assert.h>

	static void *realloc_f(void *ud, void *ptr, int osize, int nsize, const char *file, int line)
	{
		return realloc(ptr, nsize);
	}

	int main(int argc, char **argv)
	{
		struct nfcd_ConfigData *cd = nfcd_make(realloc_f, 0, 0, 0);
		assert(nfcd_type(cd, nfcd_root(cd)) == NFCD_TYPE_NULL);

		nfcd_set_root(cd, nfcd_false());
		assert(nfcd_type(cd, nfcd_root(cd)) == NFCD_TYPE_FALSE);
		nfcd_set_root(cd, nfcd_true());
		assert(nfcd_type(cd, nfcd_root(cd)) == NFCD_TYPE_TRUE);
		nfcd_set_root(cd, nfcd_null());
		assert(nfcd_type(cd, nfcd_root(cd)) == NFCD_TYPE_NULL);

		nfcd_set_root(cd, nfcd_add_number(&cd, 3.14));
		assert(nfcd_type(cd, nfcd_root(cd)) == NFCD_TYPE_NUMBER);
		assert(nfcd_to_number(cd, nfcd_root(cd)) == 3.14);

		nfcd_set_root(cd, nfcd_add_string(&cd, "str"));
		assert(nfcd_type(cd, nfcd_root(cd)) == NFCD_TYPE_STRING);
		assert(strcmp(nfcd_to_string(cd, nfcd_root(cd)), "str") == 0);

		nfcd_loc arr = nfcd_add_array(&cd, 16);
		nfcd_push(&cd, arr, nfcd_add_number(&cd, 1));
		nfcd_push(&cd, arr, nfcd_add_number(&cd, 2));
		nfcd_push(&cd, arr, nfcd_add_number(&cd, 3));
		assert(nfcd_type(cd, arr) == NFCD_TYPE_ARRAY);
		assert(nfcd_array_size(cd, arr) == 3);
		assert(nfcd_type(cd, nfcd_array_item(cd, arr, 1)) == NFCD_TYPE_NUMBER);
		assert(nfcd_to_number(cd, nfcd_array_item(cd, arr,1)) == 2);
		assert(nfcd_type(cd, nfcd_array_item(cd, arr, 10)) == NFCD_TYPE_NULL);
		
		nfcd_loc obj = nfcd_add_object(&cd, 16);
		nfcd_set_object_value(&cd, obj, "name", nfcd_add_string(&cd, "Niklas"));
		nfcd_set_object_value(&cd, obj, "age", nfcd_add_number(&cd, 41));
		
		/*

		nfcd_loc people = nfcd_make_array(cd, 10);

		nfcd_loc niklas = nfcd_make_object(cd, 5);

		// C Generics
		nfcd_set_key(cd, niklas, "first", "Niklas");
		nfcd_set_key(cd, niklas, "last", "Frykholm");
		nfcd_set_key(cd, niklas, "age", 41);

		nfcd_append(cd, people, niklas);
		*/
	}

#endif
