#ifndef _UFFS_SERIALIZE_H_
#define _UFFS_SERIALIZE_H_

#include "uffs/uffs_core.h"
#include "uffs/uffs_device.h"
#include "uffs/uffs_tree.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define UFFS_SERIALIZATION_SIZE(block_count)                                      \
	(                                                                             \
		block_count * 18 +        /* block_count * size of largest node entity */ \
		3 * 2 +                   /* Terminating indices */                       \
		DIR_NODE_ENTRY_LEN * 2 +  /* Directory node entries hashes */             \
		FILE_NODE_ENTRY_LEN * 2 + /* File node entries hashes */                  \
		DATA_NODE_ENTRY_LEN * 2   /* Data node entries hashes */                  \
	)

/*
 * The serialized state has the following form:
 *   - Collection of free entities
 *   - Collection of erased blocks
 *   - Collection of bad blocks
 *   - Collection of directory hashes
 *   - Collection of directory nodes
 *   - Collection of file hashes
 *   - Collection of file nodes
 *   - Collection of data hashes
 *   - Collection of data nodes
 * 
 * The collection of free entities is a series of 16-bit indices, where value 0xffff is a terminator.
 * 
 * The collection of erased blocks is a series of entities described below. Entity index equal to 0xffff is a terminator and means no additional entity data is present.
 * Erased block entity:
 * +--------------+-------------+
 * | Field name   | Size (bits) | 
 * +--------------+-------------+
 * | Index        | 16          |
 * | Block number | 16          |
 * | Needs check  | 8           |
 * +--------------+-------------+
 * 
 * The collection of bad blocks is a series of entities described below. Entity index equal to 0xffff is a terminator and means no additional entity data is present.
 * Bad block entity:
 * +--------------+-------------+
 * | Field name   | Size (bits) | 
 * +--------------+-------------+
 * | Index        | 16          |
 * | Block number | 16          |
 * +--------------+-------------+
 *
 * The collection of directory hashes contains exactly DIR_NODE_ENTRY_LEN 16-bit values.
 * 
 * The collection of directory nodes consists of 16-bit variable for count of its entities and a series of entities described below.
 * Directory node:
 * +---------------+-------------+
 * | Field name    | Size (bits) | 
 * +---------------+-------------+
 * | Index         | 16          |
 * | Next hash     | 16          |
 * | Previous hash | 16          |
 * | Block number  | 16          |
 * | Checksum      | 16          |
 * | Parent        | 16          |
 * | Serial        | 16          |
 * +---------------+-------------+
 *
 * The collection of file hashes contains exactly FILE_NODE_ENTRY_LEN 16-bit values.
 * 
 * The collection of file nodes consists of 16-bit variable for count of its entities and a series of entities described below.
 * File node:
 * +---------------+-------------+
 * | Field name    | Size (bits) | 
 * +---------------+-------------+
 * | Index         | 16          |
 * | Next hash     | 16          |
 * | Previous hash | 16          |
 * | Block number  | 16          |
 * | Checksum      | 16          |
 * | Parent        | 16          |
 * | Serial        | 16          |
 * | Length        | 32          |
 * +---------------+-------------+
 *
 * The collection of data hashes contains exactly DATA_NODE_ENTRY_LEN 16-bit values.
 * 
 * The collection of data nodes consists of 16-bit variable for count of its entities and a series of entities described below.
 * Data node:
 * +---------------+-------------+
 * | Field name    | Size (bits) | 
 * +---------------+-------------+
 * | Index         | 16          |
 * | Next hash     | 16          |
 * | Previous hash | 16          |
 * | Block number  | 16          |
 * | Parent        | 16          |
 * | Serial        | 16          |
 * | Length        | 32          |
 * +---------------+-------------+
 */

typedef struct uffs_SerializeOpsSt
{
	/**
	 * Begin serialization.
	 *
	 * \return 0 if no error, return -1 when failed.
	 */
	int (*BeginSerialization)(uffs_Device* dev);

	/**
	 * End serialization.
	 *
	 * \return 0 if no error, return -1 when failed.
	 */
	int (*EndSerialization)(uffs_Device* dev);

	/**
	 * Write 32-bit unsigned integer.
	 *
	 * \param[in] value value to write
	 *
	 * \return 0 if no error, return -1 when failed.
	 */
	int (*WriteU32)(uffs_Device* dev, u32 value);

	/**
	 * Write 16-bit unsigned integer.
	 *
	 * \param[in] value value to write
	 *
	 * \return 0 if no error, return -1 when failed.
	 */
	int (*WriteU16)(uffs_Device* dev, u16 value);

	/**
	 * Write 8-bit unsigned integer.
	 *
	 * \param[in] value value to write
	 *
	 * \return 0 if no error, return -1 when failed.
	 */
	int (*WriteU8)(uffs_Device* dev, u8 value);

	/**
	 * Begin deserialization.
	 *
	 * \return 0 if no error, return -1 when failed.
	 */
	int (*BeginDeserialization)(uffs_Device* dev);

	/**
	 * End deserialization.
	 */
	void (*EndDeserialization)(uffs_Device* dev);

	/**
	 * Read 32-bit unsigned integer.
	 *
	 * \param[in] value pointer to store read value
	 *
	 * \return 0 if no error, return -1 when failed.
	 */
	int (*ReadU32)(uffs_Device* dev, u32* value);

	/**
	 * Read 16-bit unsigned integer.
	 *
	 * \param[in] value pointer to store read value
	 *
	 * \return 0 if no error, return -1 when failed.
	 */
	int (*ReadU16)(uffs_Device* dev, u16* value);

	/**
	 * Read 8-bit unsigned integer.
	 *
	 * \param[in] value pointer to store read value
	 *
	 * \return 0 if no error, return -1 when failed.
	 */
	int (*ReadU8)(uffs_Device* dev, u8* value);
} uffs_SerializeOps;

/** Serialize state using operations stored in dev */
URET uffs_SerializeState(uffs_Device* dev);

/** Deserialize state using operations stored in dev */
URET uffs_DeserializeState(uffs_Device* dev);

#ifdef __cplusplus
}
#endif

#endif
