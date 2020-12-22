#include <stdint.h>
#include <string.h>
#include "uffs_config.h"
#include "uffs/uffs_types.h"
#include "uffs/uffs_public.h"
#include "uffs/uffs_tree.h"
#include "uffs/uffs_badblock.h"
#include "uffs/uffs_serialize.h"


#define PFX "serial: "

#define TO_POOL_INDEX(address, pool) (((u8 *)(address) - (pool)->mem) / (pool)->buf_size)
#define FROM_POOL_INDEX(index, pool) ((u8 *)(pool)->mem + (index) * (pool)->buf_size)


static UBOOL IsValidTreeAddress(uffs_Device *dev, void *address) {
	if ((u8*)address < dev->mem.tree_pool.mem) {
		return U_FALSE;
	}

	if ((u8*)address > (dev->mem.tree_pool.mem + dev->mem.tree_pool.buf_size * dev->mem.tree_pool.num_bufs)) {
		return U_FALSE;
	}

	if ((((u8*)address - dev->mem.tree_pool.mem) % dev->mem.tree_pool.buf_size) != 0) {
		return U_FALSE;
	}

	return U_TRUE;
}

static URET SerializeIndex(uffs_Device *dev, void *address) {
	uffs_SerializeOps *ops;

	ops = dev->serial_ops;

	if (address == NULL) {
		if (ops->WriteU16(dev, (u16)(-1)) < 0) {
	        return U_FAIL;
		}
	} else {
		if (ops->WriteU16(dev, (u16)TO_POOL_INDEX(address, &dev->mem.tree_pool)) < 0) {
	        return U_FAIL;
		}
	}

	return U_SUCC;
}

static URET DeserializeIndex(uffs_Device *dev, void **address) {
	u16 index;
	uffs_SerializeOps *ops;

	ops = dev->serial_ops;

	if (ops->ReadU16(dev, &index) < 0) {
		return U_FAIL;
	}

	if (index == (u16)-1) {
		*address = NULL;
	} else {
		*address = FROM_POOL_INDEX(index, &dev->mem.tree_pool);
		if (!IsValidTreeAddress(dev, *address)) {
			return U_FALSE;
		}
	}

	return U_SUCC;
}

static URET SerializeFreeEntries(uffs_Device *dev) {
	uffs_PoolEntry *entry;

	entry = dev->mem.tree_pool.free_list;
	while (entry != NULL) {
		if (SerializeIndex(dev, entry) != U_SUCC) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot write free entry index");
			return U_FAIL;
		}

		entry = entry->next;
	}

	if (SerializeIndex(dev, entry) != U_SUCC) {
		uffs_Perror(UFFS_MSG_SERIOUS, "cannot write free entry index");
		return U_FAIL;
	}

	return U_SUCC;
}

static URET DeserializeFreeEntries(uffs_Device *dev) {
	uffs_PoolEntry *entry;

	if (DeserializeIndex(dev, (void**)&dev->mem.tree_pool.free_list) != U_SUCC) {
		uffs_Perror(UFFS_MSG_SERIOUS, "cannot read free entry index");
		return U_FAIL;
	}

	entry = dev->mem.tree_pool.free_list;
	while (entry != NULL) {
		if (DeserializeIndex(dev, (void**)&entry->next) != U_SUCC) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot read free entry index");
			return U_FAIL;
		}

		entry = entry->next;
	}

	return U_SUCC;
}

static URET SerializeErasedBlocks(uffs_Device *dev) {
	TreeNode *node;
	uffs_SerializeOps *ops;

	ops = dev->serial_ops;

	node = dev->tree.erased;
	while (node != NULL) {
		if (SerializeIndex(dev, node) != U_SUCC) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot write erased block index");
			return U_FAIL;
		}

		if (ops->WriteU16(dev, node->u.list.block) < 0) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot write block number");
		}

		if (ops->WriteU8(dev, node->u.list.u.need_check) < 0) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot write need check flag");
		}

		node = node->u.list.next;
	}

	if (SerializeIndex(dev, node) != U_SUCC) {
		uffs_Perror(UFFS_MSG_SERIOUS, "cannot write terminating erased block index");
		return U_FAIL;
	}

	return U_SUCC;
}

static URET DeserializeErasedBlocks(uffs_Device *dev) {
	TreeNode *node;
	uffs_SerializeOps *ops;

	ops = dev->serial_ops;

	if (DeserializeIndex(dev, (void**)&dev->tree.erased) != U_SUCC) {
		uffs_Perror(UFFS_MSG_SERIOUS, "cannot read erased block index");
		return U_FAIL;
	}

	dev->tree.erased_count = 0;
	dev->tree.erased_tail = dev->tree.erased;
	node = dev->tree.erased;
	while (node != NULL) {
		if (ops->ReadU16(dev, &node->u.list.block) < 0) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot read block number");
		}

		if (ops->ReadU8(dev, &node->u.list.u.need_check) < 0) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot read need check flag");
		}

		if (DeserializeIndex(dev, (void**)&node->u.list.next) != U_SUCC) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot read next erased block index");
			return U_FAIL;
		}

		if (node->u.list.next != NULL) {
			node->u.list.next->u.list.prev = node;
		}

		dev->tree.erased_tail = node;
		dev->tree.erased_count++;
		node = node->u.list.next;
	}

	return U_SUCC;
}

static URET SerializeBadBlocks(uffs_Device *dev) {
	TreeNode *node;
	uffs_SerializeOps *ops;

	ops = dev->serial_ops;

	node = dev->tree.bad;
	while (node != NULL) {
		if (SerializeIndex(dev, node) != U_SUCC) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot write bad block index");
			return U_FAIL;
		}

		if (ops->WriteU16(dev, node->u.list.block) < 0) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot write block number");
			return U_FAIL;
		}

		node = node->u.list.next;
	}

	if (SerializeIndex(dev, node) != U_SUCC) {
		uffs_Perror(UFFS_MSG_SERIOUS, "cannot write terminating bad block index");
		return U_FAIL;
	}

	return U_SUCC;
}

static URET DeserializeBadBlocks(uffs_Device *dev) {
	TreeNode *node;
	uffs_SerializeOps *ops;

	ops = dev->serial_ops;

	if (DeserializeIndex(dev, (void**)&dev->tree.bad) != U_SUCC) {
		uffs_Perror(UFFS_MSG_SERIOUS, "cannot read bad block index");
		return U_FAIL;
	}

	dev->tree.bad_count = 0;
	node = dev->tree.bad;
	while (node != NULL) {
		if (ops->ReadU16(dev, &node->u.list.block) < 0) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot read block number");
			return U_FAIL;
		}

		if (DeserializeIndex(dev, (void**)&node->u.list.next) != U_SUCC) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot read next bad block index");
			return U_FAIL;
		}

		if (node->u.list.next != NULL) {
			node->u.list.next->u.list.prev = node;
		}

		dev->tree.bad_count++;
		node = node->u.list.next;
	}

	return U_SUCC;
}

static URET SerializeDirNodes(uffs_Device *dev) {
	u16 hash;
	u16 index;
	u16 nodes_count;
	TreeNode *node;
	uffs_SerializeOps *ops;

	ops = dev->serial_ops;

	nodes_count = 0;
	for (hash = 0; hash < DIR_NODE_ENTRY_LEN; hash++) {
		if (ops->WriteU16(dev, dev->tree.dir_entry[hash]) < 0) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot write dir hash");
			return U_FAIL;
		}

		index = dev->tree.dir_entry[hash];
		while (index != EMPTY_NODE) {
			node = (TreeNode *)FROM_POOL_INDEX(index, &dev->mem.tree_pool);
			index = node->hash_next;
			nodes_count++;
		}
	}

	if (ops->WriteU16(dev, nodes_count) < 0) {
		uffs_Perror(UFFS_MSG_SERIOUS, "cannot write dir nodes count");
		return U_FAIL;
	}

	for (hash = 0; hash < DIR_NODE_ENTRY_LEN; hash++) {
		index = dev->tree.dir_entry[hash];
		while (index != EMPTY_NODE) {
			node = (TreeNode *)FROM_POOL_INDEX(index, &dev->mem.tree_pool);
			if (SerializeIndex(dev, node) != U_SUCC) {
				uffs_Perror(UFFS_MSG_SERIOUS, "cannot write dir node index");
				return U_FAIL;
			}

			if (ops->WriteU16(dev, node->hash_next) < 0) {
				uffs_Perror(UFFS_MSG_SERIOUS, "cannot write next hash");
				return U_FAIL;
			}

			if (ops->WriteU16(dev, node->hash_prev) < 0) {
				uffs_Perror(UFFS_MSG_SERIOUS, "cannot write prev hash");
				return U_FAIL;
			}

			if (ops->WriteU16(dev, node->u.dir.block) < 0) {
				uffs_Perror(UFFS_MSG_SERIOUS, "cannot write dir block number");
				return U_FAIL;
			}

			if (ops->WriteU16(dev, node->u.dir.checksum) < 0) {
				uffs_Perror(UFFS_MSG_SERIOUS, "cannot write dir checksum");
				return U_FAIL;
			}

			if (ops->WriteU16(dev, node->u.dir.parent) < 0) {
				uffs_Perror(UFFS_MSG_SERIOUS, "cannot write dir parent");
				return U_FAIL;
			}

			if (ops->WriteU16(dev, node->u.dir.serial) < 0) {
				uffs_Perror(UFFS_MSG_SERIOUS, "cannot write dir serial");
				return U_FAIL;
			}

			index = node->hash_next;
		}
	}

	return U_SUCC;
}

static URET DeserializeDirNodes(uffs_Device *dev) {
	u16 index;
	u16 nodes_count;
	TreeNode *node;
	uffs_SerializeOps *ops;

	ops = dev->serial_ops;

	nodes_count = 0;
	for (index = 0; index < DIR_NODE_ENTRY_LEN; index++) {
		if (ops->ReadU16(dev, &dev->tree.dir_entry[index]) < 0) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot read dir hash");
			return U_FAIL;
		}
	}

	if (ops->ReadU16(dev, &nodes_count) < 0) {
		uffs_Perror(UFFS_MSG_SERIOUS, "cannot read dir nodes count");
		return U_FAIL;
	}

	for (index = 0; index < nodes_count; index++) {
		if (DeserializeIndex(dev, (void**)&node) != U_SUCC) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot read dir node index");
			return U_FAIL;
		}

		if (ops->ReadU16(dev, &node->hash_next) < 0) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot read next hash");
			return U_FAIL;
		}

		if (ops->ReadU16(dev, &node->hash_prev) < 0) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot read prev hash");
			return U_FAIL;
		}

		if (ops->ReadU16(dev, &node->u.dir.block) < 0) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot read dir block number");
			return U_FAIL;
		}

		if (ops->ReadU16(dev, &node->u.dir.checksum) < 0) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot read dir checksum");
			return U_FAIL;
		}

		if (ops->ReadU16(dev, &node->u.dir.parent) < 0) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot read dir parent");
			return U_FAIL;
		}

		if (ops->ReadU16(dev, &node->u.dir.serial) < 0) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot read dir serial");
			return U_FAIL;
		}
	}

	return U_SUCC;
}

static URET SerializeFileNodes(uffs_Device *dev) {
	u16 hash;
	u16 index;
	u16 nodes_count;
	TreeNode *node;
	uffs_SerializeOps *ops;

	ops = dev->serial_ops;

	nodes_count = 0;
	for (hash = 0; hash < FILE_NODE_ENTRY_LEN; hash++) {
		if (ops->WriteU16(dev, dev->tree.file_entry[hash]) < 0) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot write file hash");
			return U_FAIL;
		}

		index = dev->tree.file_entry[hash];
		while (index != EMPTY_NODE) {
			node = (TreeNode *)FROM_POOL_INDEX(index, &dev->mem.tree_pool);
			index = node->hash_next;
			nodes_count++;
		}
	}

	if (ops->WriteU16(dev, nodes_count) < 0) {
		uffs_Perror(UFFS_MSG_SERIOUS, "cannot write file nodes count");
		return U_FAIL;
	}

	for (hash = 0; hash < FILE_NODE_ENTRY_LEN; hash++) {
		index = dev->tree.file_entry[hash];
		while (index != EMPTY_NODE) {
			node = (TreeNode *)FROM_POOL_INDEX(index, &dev->mem.tree_pool);
			if (SerializeIndex(dev, node) != U_SUCC) {
				uffs_Perror(UFFS_MSG_SERIOUS, "cannot write file node index");
				return U_FAIL;
			}

			if (ops->WriteU16(dev, node->hash_next) < 0) {
				uffs_Perror(UFFS_MSG_SERIOUS, "cannot write next hash");
				return U_FAIL;
			}

			if (ops->WriteU16(dev, node->hash_prev) < 0) {
				uffs_Perror(UFFS_MSG_SERIOUS, "cannot write prev hash");
				return U_FAIL;
			}

			if (ops->WriteU16(dev, node->u.file.block) < 0) {
				uffs_Perror(UFFS_MSG_SERIOUS, "cannot write file block number");
				return U_FAIL;
			}

			if (ops->WriteU16(dev, node->u.file.checksum) < 0) {
				uffs_Perror(UFFS_MSG_SERIOUS, "cannot write file checksum");
				return U_FAIL;
			}

			if (ops->WriteU16(dev, node->u.file.parent) < 0) {
				uffs_Perror(UFFS_MSG_SERIOUS, "cannot write file parent");
				return U_FAIL;
			}

			if (ops->WriteU16(dev, node->u.file.serial) < 0) {
				uffs_Perror(UFFS_MSG_SERIOUS, "cannot write file serial");
				return U_FAIL;
			}

			if (ops->WriteU32(dev, node->u.file.len) < 0) {
				uffs_Perror(UFFS_MSG_SERIOUS, "cannot write file len");
				return U_FAIL;
			}

			index = node->hash_next;
		}
	}

	return U_SUCC;
}

static URET DeserializeFileNodes(uffs_Device *dev) {
	u16 index;
	u16 nodes_count;
	TreeNode *node;
	uffs_SerializeOps *ops;

	ops = dev->serial_ops;

	nodes_count = 0;
	for (index = 0; index < FILE_NODE_ENTRY_LEN; index++) {
		if (ops->ReadU16(dev, &dev->tree.file_entry[index]) < 0) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot read file hash");
			return U_FAIL;
		}
	}

	if (ops->ReadU16(dev, &nodes_count) < 0) {
		uffs_Perror(UFFS_MSG_SERIOUS, "cannot read file nodes count");
		return U_FAIL;
	}

	for (index = 0; index < nodes_count; index++) {
		if (DeserializeIndex(dev, (void**)&node) != U_SUCC) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot read file node index");
			return U_FAIL;
		}

		if (ops->ReadU16(dev, &node->hash_next) < 0) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot read next hash");
			return U_FAIL;
		}

		if (ops->ReadU16(dev, &node->hash_prev) < 0) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot read prev hash");
			return U_FAIL;
		}

		if (ops->ReadU16(dev, &node->u.file.block) < 0) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot read file block number");
			return U_FAIL;
		}

		if (ops->ReadU16(dev, &node->u.file.checksum) < 0) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot read file checksum");
			return U_FAIL;
		}

		if (ops->ReadU16(dev, &node->u.file.parent) < 0) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot read file parent");
			return U_FAIL;
		}

		if (ops->ReadU16(dev, &node->u.file.serial) < 0) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot read file serial");
			return U_FAIL;
		}

		if (ops->ReadU32(dev, &node->u.file.len) < 0) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot read file len");
			return U_FAIL;
		}
	}

	return U_SUCC;
}

static URET SerializeDataNodes(uffs_Device *dev) {
	u16 hash;
	u16 index;
	u16 nodes_count;
	TreeNode *node;
	uffs_SerializeOps *ops;

	ops = dev->serial_ops;

	nodes_count = 0;
	for (hash = 0; hash < DATA_NODE_ENTRY_LEN; hash++) {
		if (ops->WriteU16(dev, dev->tree.data_entry[hash]) < 0) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot write data hash");
			return U_FAIL;
		}

		index = dev->tree.data_entry[hash];
		while (index != EMPTY_NODE) {
			node = (TreeNode *)FROM_POOL_INDEX(index, &dev->mem.tree_pool);
			index = node->hash_next;
			nodes_count++;
		}
	}

	if (ops->WriteU16(dev, nodes_count) < 0) {
		uffs_Perror(UFFS_MSG_SERIOUS, "cannot write data nodes count");
		return U_FAIL;
	}

	for (hash = 0; hash < DATA_NODE_ENTRY_LEN; hash++) {
		index = dev->tree.data_entry[hash];
		while (index != EMPTY_NODE) {
			node = (TreeNode *)FROM_POOL_INDEX(index, &dev->mem.tree_pool);
			if (SerializeIndex(dev, node) != U_SUCC) {
				uffs_Perror(UFFS_MSG_SERIOUS, "cannot write data node index");
				return U_FAIL;
			}

			if (ops->WriteU16(dev, node->hash_next) < 0) {
				uffs_Perror(UFFS_MSG_SERIOUS, "cannot write next hash");
				return U_FAIL;
			}

			if (ops->WriteU16(dev, node->hash_prev) < 0) {
				uffs_Perror(UFFS_MSG_SERIOUS, "cannot write prev hash");
				return U_FAIL;
			}

			if (ops->WriteU16(dev, node->u.data.block) < 0) {
				uffs_Perror(UFFS_MSG_SERIOUS, "cannot write data block number");
				return U_FAIL;
			}

			if (ops->WriteU16(dev, node->u.data.parent) < 0) {
				uffs_Perror(UFFS_MSG_SERIOUS, "cannot write data parent");
				return U_FAIL;
			}

			if (ops->WriteU16(dev, node->u.data.serial) < 0) {
				uffs_Perror(UFFS_MSG_SERIOUS, "cannot write data serial");
				return U_FAIL;
			}

			if (ops->WriteU32(dev, node->u.data.len) < 0) {
				uffs_Perror(UFFS_MSG_SERIOUS, "cannot write data len");
				return U_FAIL;
			}

			index = node->hash_next;
		}
	}

	return U_SUCC;
}

static URET DeserializeDataNodes(uffs_Device *dev) {
	u16 index;
	u16 nodes_count;
	TreeNode *node;
	uffs_SerializeOps *ops;

	ops = dev->serial_ops;

	nodes_count = 0;
	for (index = 0; index < DATA_NODE_ENTRY_LEN; index++) {
		if (ops->ReadU16(dev, &dev->tree.data_entry[index]) < 0) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot read data hash");
			return U_FAIL;
		}
	}

	if (ops->ReadU16(dev, &nodes_count) < 0) {
		uffs_Perror(UFFS_MSG_SERIOUS, "cannot read data nodes count");
		return U_FAIL;
	}

	for (index = 0; index < nodes_count; index++) {
		if (DeserializeIndex(dev, (void**)&node) != U_SUCC) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot read data node index");
			return U_FAIL;
		}

		if (ops->ReadU16(dev, &node->hash_next) < 0) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot read next hash");
			return U_FAIL;
		}

		if (ops->ReadU16(dev, &node->hash_prev) < 0) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot read prev hash");
			return U_FAIL;
		}

		if (ops->ReadU16(dev, &node->u.data.block) < 0) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot write data block number");
			return U_FAIL;
		}

		if (ops->ReadU16(dev, &node->u.data.parent) < 0) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot write data parent");
			return U_FAIL;
		}

		if (ops->ReadU16(dev, &node->u.data.serial) < 0) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot write data serial");
			return U_FAIL;
		}

		if (ops->ReadU32(dev, &node->u.data.len) < 0) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot write data len");
			return U_FAIL;
		}
	}

	return U_SUCC;
}

URET uffs_SerializeState(uffs_Device *dev) {
	uffs_SerializeOps *ops;

	ops = dev->serial_ops;
	if (ops == NULL) {
		uffs_Perror(UFFS_MSG_NORMAL, "serialization operations are not set");
		return U_FAIL;
	}

	if (ops->BeginSerialization != NULL) {
		if (ops->BeginSerialization(dev) < 0) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot begin serialization");
			return U_FAIL;
		}
	}

	if (SerializeFreeEntries(dev) != U_SUCC) {
		uffs_Perror(UFFS_MSG_SERIOUS, "cannot serialize free nodes");
		return U_FAIL;
	}

	if (SerializeErasedBlocks(dev) != U_SUCC) {
		uffs_Perror(UFFS_MSG_SERIOUS, "cannot serialize erased blocks");
		return U_FAIL;
	}

	if (SerializeBadBlocks(dev) != U_SUCC) {
		uffs_Perror(UFFS_MSG_SERIOUS, "cannot serialize bad blocks");
		return U_FAIL;
	}

	if (SerializeDirNodes(dev) != U_SUCC) {
		uffs_Perror(UFFS_MSG_SERIOUS, "cannot serialize dir nodes");
		return U_FAIL;
	}

	if (SerializeFileNodes(dev) != U_SUCC) {
		uffs_Perror(UFFS_MSG_SERIOUS, "cannot serialize file nodes");
		return U_FAIL;
	}

	if (SerializeDataNodes(dev) != U_SUCC) {
		uffs_Perror(UFFS_MSG_SERIOUS, "cannot serialize data nodes");
		return U_FAIL;
	}

	if (ops->EndSerialization != NULL) {
		if (ops->EndSerialization(dev) < 0) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot end serialization");
			return U_FAIL;
		}
	}

	return U_SUCC;
}

static URET DeserializeState(uffs_Device *dev) {
	uffs_SerializeOps *ops;

	ops = dev->serial_ops;
	if (ops == NULL) {
		uffs_Perror(UFFS_MSG_NORMAL, "deserialization operations are not set");
		return U_FAIL;
	}

	if (ops->BeginDeserialization != NULL) {
		if (ops->BeginDeserialization(dev) < 0) {
			uffs_Perror(UFFS_MSG_SERIOUS, "cannot begin deserialization");
			return U_FAIL;
		}
	}

	if (DeserializeFreeEntries(dev) != U_SUCC) {
		uffs_Perror(UFFS_MSG_SERIOUS, "cannot deserialize free nodes");
		return U_FAIL;
	}

	if (DeserializeErasedBlocks(dev) != U_SUCC) {
		uffs_Perror(UFFS_MSG_SERIOUS, "cannot deserialize erased blocks");
		return U_FAIL;
	}

	if (DeserializeBadBlocks(dev) != U_SUCC) {
		uffs_Perror(UFFS_MSG_SERIOUS, "cannot deserialize bad blocks");
		return U_FAIL;
	}

	if (DeserializeDirNodes(dev) != U_SUCC) {
		uffs_Perror(UFFS_MSG_SERIOUS, "cannot deserialize dir nodes");
		return U_FAIL;
	}

	if (DeserializeFileNodes(dev) != U_SUCC) {
		uffs_Perror(UFFS_MSG_SERIOUS, "cannot deserialize file nodes");
		return U_FAIL;
	}

	if (DeserializeDataNodes(dev) != U_SUCC) {
		uffs_Perror(UFFS_MSG_SERIOUS, "cannot deserialize data nodes");
		return U_FAIL;
	}

	if (ops->EndDeserialization != NULL) {
		ops->EndDeserialization(dev);
	}

	return U_SUCC;
}

static void ResetState(uffs_Device *dev) {
	u16 index;

	memset(dev->mem.tree_pool.mem, 0, dev->mem.tree_pool.buf_size * dev->mem.tree_pool.num_bufs);

	dev->tree.erased = NULL;
	dev->tree.erased_tail = NULL;
	dev->tree.erased_count = 0;
	dev->tree.bad = NULL;
	dev->tree.bad_count = 0;

	for (index = 0; index < DIR_NODE_ENTRY_LEN; index++) {
		dev->tree.dir_entry[index] = EMPTY_NODE;
	}

	for (index = 0; index < FILE_NODE_ENTRY_LEN; index++) {
		dev->tree.file_entry[index] = EMPTY_NODE;
	}

	for (index = 0; index < DATA_NODE_ENTRY_LEN; index++) {
		dev->tree.data_entry[index] = EMPTY_NODE;
	}

	dev->mem.tree_pool.free_list = ((uffs_PoolEntry *)FROM_POOL_INDEX(0, &dev->mem.tree_pool));
	for (index = 1; index < dev->mem.tree_pool.num_bufs; index++) {
		((uffs_PoolEntry *)FROM_POOL_INDEX(index - 1, &dev->mem.tree_pool))->next = ((uffs_PoolEntry *)FROM_POOL_INDEX(index, &dev->mem.tree_pool));
	}

	((uffs_PoolEntry *)FROM_POOL_INDEX(index - 1, &dev->mem.tree_pool))->next = NULL;
}

URET uffs_DeserializeState(uffs_Device *dev) {
	if (DeserializeState(dev) != U_SUCC) {
		ResetState(dev);
		return U_FAIL;
	}

	return U_SUCC;
}
