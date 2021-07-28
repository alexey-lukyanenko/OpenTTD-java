/* $Id: saveload.c 3294 2005-12-13 21:21:57Z Darkvater $ */

/** @file
 * All actions handling saving and loading goes on in this file. The general actions
 * are as follows for saving a game (loading is analogous):
 * <ol>
 * <li>initialize the writer by creating a temporary memory-buffer for it
 * <li>go through all to-be saved elements, each 'chunk' (ChunkHandler) prefixed by a label
 * <li>use their description array (SaveLoad) to know what elements to save and in what version
 *    of the game it was active (used when loading)
 * <li>write all data byte-by-byte to the temporary buffer so it is endian-safe
 * <li>when the buffer is full; flush it to the output (eg save to file) (_sl.buf, _sl.bufp, _sl.bufe)
 * <li>repeat this until everything is done, and flush any remaining output to file
 * </ol>
 * @see ChunkHandler
 * @see SaveLoad
 */
#include "stdafx.h"
#include "openttd.h"
#include "debug.h"
#include "functions.h"
#include "vehicle.h"
#include "station.h"
#include "thread.h"
#include "town.h"
#include "player.h"
#include "saveload.h"
#include "variables.h"
#include <setjmp.h>

enum {
	SAVEGAME_VERSION = 18,

};

uint16 _sl_version;       /// the major savegame version identifier
byte   _sl_minor_version; /// the minor savegame version, DO NOT USE!

typedef void WriterProc(uint len);
typedef uint ReaderProc(void);

typedef uint ReferenceToIntProc(const void *obj, SLRefType rt);
typedef void *IntToReferenceProc(uint index, SLRefType rt);

/** The saveload struct, containing reader-writer functions, bufffer, version, etc. */
static struct {
	bool save;                           /// are we doing a save or a load atm. True when saving
	byte need_length;                    /// ???
	byte block_mode;                     /// ???
	bool error;                          /// did an error occur or not

	int obj_len;                         /// the length of the current object we are busy with
	int array_index, last_array_index;   /// in the case of an array, the current and last positions

	uint32 offs_base;                    /// the offset in number of bytes since we started writing data (eg uncompressed savegame size)

	WriterProc *write_bytes;             /// savegame writer function
	ReaderProc *read_bytes;              /// savegame loader function

	ReferenceToIntProc *ref_to_int_proc; /// function to convert pointers to numbers when saving a game
	IntToReferenceProc *int_to_ref_proc; /// function to convert numbers to pointers when loading a game

	const ChunkHandler* const *chs;      /// the chunk of data that is being processed atm (vehicles, signs, etc.)
	const SaveLoad* const *includes;     /// the internal layouf of the given chunk

	/** When saving/loading savegames, they are always saved to a temporary memory-place
	 * to be flushed to file (save) or to final place (load) when full. */
	byte *bufp, *bufe;                   /// bufp(ointer) gives the current position in the buffer bufe(nd) gives the end of the buffer

	// these 3 may be used by compressor/decompressors.
	byte *buf;                           /// pointer to temporary memory to read/write, initialized by SaveLoadFormat->initread/write
	byte *buf_ori;                       /// pointer to the original memory location of buf, used to free it afterwards
	uint bufsize;                        /// the size of the temporary memory *buf
	FILE *fh;                            /// the file from which is read or written to

	void (*excpt_uninit)(void);          /// the function to execute on any encountered error
	const char *excpt_msg;               /// the error message
	jmp_buf excpt;                       /// @todo used to jump to "exception handler";  really ugly
} _sl;


enum NeedLengthValues {NL_NONE = 0, NL_WANTLENGTH = 1, NL_CALCLENGTH = 2};

/**
 * Fill the input buffer by reading from the file with the given reader
 */
static void SlReadFill(void)
{
	uint len = _sl.read_bytes();
	assert(len != 0);

	_sl.bufp = _sl.buf;
	_sl.bufe = _sl.buf + len;
	_sl.offs_base += len;
}

static inline uint32 SlGetOffs(void) {return _sl.offs_base - (_sl.bufe - _sl.bufp);}

/** Flush the output buffer by writing to disk with the given reader.
 * If the buffer pointer has not yet been set up, set it up now. Usually
 * only called when the buffer is full, or there is no more data to be processed
 */
static void SlWriteFill(void)
{
	// flush the buffer to disk (the writer)
	if (_sl.bufp != NULL) {
		uint len = _sl.bufp - _sl.buf;
		_sl.offs_base += len;
		if (len) _sl.write_bytes(len);
	}

	/* All the data from the buffer has been written away, rewind to the beginning
	* to start reading in more data */
	_sl.bufp = _sl.buf;
	_sl.bufe = _sl.buf + _sl.bufsize;
}

/** Error handler, calls longjmp to simulate an exception.
 * @todo this was used to have a central place to handle errors, but it is
 * pretty ugly, and seriously interferes with any multithreaded approaches */
static void NORETURN SlError(const char *msg)
{
	_sl.excpt_msg = msg;
	longjmp(_sl.excpt, 0);
}

/** Read in a single byte from file. If the temporary buffer is full,
 * flush it to its final destination
 * @return return the read byte from file
 */
static inline int SlReadByteInternal(void)
{
	if (_sl.bufp == _sl.bufe) SlReadFill();
	return *_sl.bufp++;
}

/** Wrapper for SlReadByteInternal */
int SlReadByte(void) {return SlReadByteInternal();}

/** Write away a single byte from memory. If the temporary buffer is full,
 * flush it to its destination (file)
 * @param b the byte that is currently written
 */
static inline void SlWriteByteInternal(byte b)
{
	if (_sl.bufp == _sl.bufe) SlWriteFill();
	*_sl.bufp++ = b;
}

/** Wrapper for SlWriteByteInternal */
void SlWriteByte(byte b) {SlWriteByteInternal(b);}

static inline int SlReadUint16(void)
{
	int x = SlReadByte() << 8;
	return x | SlReadByte();
}

static inline uint32 SlReadUint32(void)
{
	uint32 x = SlReadUint16() << 16;
	return x | SlReadUint16();
}

static inline uint64 SlReadUint64(void)
{
	uint32 x = SlReadUint32();
	uint32 y = SlReadUint32();
	return (uint64)x << 32 | y;
}

static inline void SlWriteUint16(uint16 v)
{
	SlWriteByte(GB(v, 8, 8));
	SlWriteByte(GB(v, 0, 8));
}

static inline void SlWriteUint32(uint32 v)
{
	SlWriteUint16(GB(v, 16, 16));
	SlWriteUint16(GB(v,  0, 16));
}

static inline void SlWriteUint64(uint64 x)
{
	SlWriteUint32((uint32)(x >> 32));
	SlWriteUint32((uint32)x);
}

/**
 * Read in the header descriptor of an object or an array.
 * If the highest bit is set (7), then the index is bigger than 127
 * elements, so use the next byte to read in the real value.
 * The actual value is then both bytes added with the first shifted
 * 8 bits to the left, and dropping the highest bit (which only indicated a big index).
 * x = ((x & 0x7F) << 8) + SlReadByte();
 * @return Return the value of the index
 */
static uint SlReadSimpleGamma(void)
{
	uint i = SlReadByte();
	if (HASBIT(i, 7)) {
		i &= ~0x80;
		if (HASBIT(i, 6)) {
			i &= ~0x40;
			if (HASBIT(i, 5)) {
				i &= ~0x20;
				if (HASBIT(i, 4))
					SlError("Unsupported gamma");
				i = (i << 8) | SlReadByte();
			}
			i = (i << 8) | SlReadByte();
		}
		i = (i << 8) | SlReadByte();
	}
	return i;
}

/**
 * Write the header descriptor of an object or an array.
 * If the element is bigger than 127, use 2 bytes for saving
 * and use the highest byte of the first written one as a notice
 * that the length consists of 2 bytes, etc.. like this:
 * 0xxxxxxx
 * 10xxxxxx xxxxxxxx
 * 110xxxxx xxxxxxxx xxxxxxxx
 * 1110xxxx xxxxxxxx xxxxxxxx xxxxxxxx
 * @param i Index being written
 */

static void SlWriteSimpleGamma(uint i)
{
	if (i >= (1 << 7)) {
		if (i >= (1 << 14)) {
			if (i >= (1 << 21)) {
				assert(i < (1 << 28));
				SlWriteByte((byte)0xE0 | (i>>24));
				SlWriteByte((byte)(i>>16));
			} else {
				SlWriteByte((byte)0xC0 | (i>>16));
			}
			SlWriteByte((byte)(i>>8));
		} else {
			SlWriteByte((byte)(0x80 | (i>>8)));
		}
	}
	SlWriteByte(i);
}

/** Return how many bytes used to encode a gamma value */
static inline uint SlGetGammaLength(uint i) {
	return 1 + (i >= (1 << 7)) + (i >= (1 << 14)) + (i >= (1 << 21));
}

static inline int SlReadSparseIndex(void) {return SlReadSimpleGamma();}
static inline void SlWriteSparseIndex(uint index) {SlWriteSimpleGamma(index);}

static inline int SlReadArrayLength(void) {return SlReadSimpleGamma();}
static inline void SlWriteArrayLength(uint length) {SlWriteSimpleGamma(length);}

void SlSetArrayIndex(uint index)
{
	_sl.need_length = NL_WANTLENGTH;
	_sl.array_index = index;
}

/**
 * Iterate through the elements of an array and read the whole thing
 * @return The index of the object, or -1 if we have reached the end of current block
 */
int SlIterateArray(void)
{
	int index;
	static uint32 next_offs;

	/* After reading in the whole array inside the loop
	 * we must have read in all the data, so we must be at end of current block. */
	assert(next_offs == 0 || SlGetOffs() == next_offs);

	while (true) {
		uint length = SlReadArrayLength();
		if (length == 0) {
			next_offs = 0;
			return -1;
		}

		_sl.obj_len = --length;
		next_offs = SlGetOffs() + length;

		switch (_sl.block_mode) {
		case CH_SPARSE_ARRAY: index = SlReadSparseIndex(); break;
		case CH_ARRAY:        index = _sl.array_index++; break;
		default:
			DEBUG(misc, 0) ("SlIterateArray: error");
			return -1; // error
		}

		if (length != 0) return index;
	}
}

/**
 * Sets the length of either a RIFF object or the number of items in an array.
 * This lets us load an object or an array of arbitrary size
 * @param length The length of the sought object/array
 */
void SlSetLength(size_t length)
{
	switch (_sl.need_length) {
	case NL_WANTLENGTH:
		_sl.need_length = NL_NONE;
		switch (_sl.block_mode) {
		case CH_RIFF:
			// Ugly encoding of >16M RIFF chunks
			// The lower 24 bits are normal
			// The uppermost 4 bits are bits 24:27
			assert(length < (1<<28));
			SlWriteUint32((length & 0xFFFFFF) | ((length >> 24) << 28));
			break;
		case CH_ARRAY:
			assert(_sl.last_array_index <= _sl.array_index);
			while (++_sl.last_array_index <= _sl.array_index)
				SlWriteArrayLength(1);
			SlWriteArrayLength(length + 1);
			break;
		case CH_SPARSE_ARRAY:
			SlWriteArrayLength(length + 1 + SlGetGammaLength(_sl.array_index)); // Also include length of sparse index.
			SlWriteSparseIndex(_sl.array_index);
			break;
		default: NOT_REACHED();
		} break;
	case NL_CALCLENGTH:
		_sl.obj_len += length;
		break;
	}
}

/**
 * Save/Load bytes. These do not need to be converted to Little/Big Endian
 * so directly write them or read them to/from file
 * @param ptr The source or destination of the object being manipulated
 * @param length number of bytes this fast CopyBytes lasts
 */
static void SlCopyBytes(void *ptr, size_t length)
{
	byte *p = (byte*)ptr;

	if (_sl.save) {
		for (; length != 0; length--) {SlWriteByteInternal(*p++);}
	} else {
		for (; length != 0; length--) {*p++ = SlReadByteInternal();}
	}
}

#if 0
/**
 * Read in bytes from the file/data structure but don't do
 * anything with them
 * NOTICE: currently unused
 * @param length The amount of bytes that is being treated this way
 */
static inline void SlSkipBytes(size_t length)
{
	for (; length != 0; length--)
		SlReadByte();
}
#endif

/* Get the length of the current object */
uint SlGetFieldLength(void) {return _sl.obj_len;}

/**
 * Handle all conversion and typechecking of variables here.
 * In the case of saving, read in the actual value from the struct
 * and then write them to file, endian safely. Loading a value
 * goes exactly the opposite way
 * @param ptr The object being filled/read
 * @param conv @VarType type of the current element of the struct
 */
static void SlSaveLoadConv(void *ptr, VarType conv)
{
	int64 x = 0;

	if (_sl.save) { /* SAVE values */
		/* Read a value from the struct. These ARE endian safe. */
		switch ((conv >> 4) & 0xF) {
		case SLE_VAR_I8   >> 4: x = *(int8*)ptr; break;
		case SLE_VAR_U8   >> 4: x = *(byte*)ptr; break;
		case SLE_VAR_I16  >> 4: x = *(int16*)ptr; break;
		case SLE_VAR_U16  >> 4: x = *(uint16*)ptr; break;
		case SLE_VAR_I32  >> 4: x = *(int32*)ptr; break;
		case SLE_VAR_U32  >> 4: x = *(uint32*)ptr; break;
		case SLE_VAR_I64  >> 4: x = *(int64*)ptr; break;
		case SLE_VAR_U64  >> 4: x = *(uint64*)ptr; break;
		case SLE_VAR_NULL >> 4: x = 0; break;
		default: NOT_REACHED();
		}

		// Write the value to the file and check if its value is in the desired range
		switch (conv & 0xF) {
		case SLE_FILE_I8: assert(x >= -128 && x <= 127);     SlWriteByte(x);break;
		case SLE_FILE_U8: assert(x >= 0 && x <= 255);        SlWriteByte(x);break;
		case SLE_FILE_I16:assert(x >= -32768 && x <= 32767); SlWriteUint16(x);break;
		case SLE_FILE_STRINGID:
		case SLE_FILE_U16:assert(x >= 0 && x <= 65535);      SlWriteUint16(x);break;
		case SLE_FILE_I32: case SLE_FILE_U32:                SlWriteUint32((uint32)x);break;
		case SLE_FILE_I64: case SLE_FILE_U64:                SlWriteUint64(x);break;
		default: NOT_REACHED();
		}
	} else { /* LOAD values */

		// Read a value from the file
		switch (conv & 0xF) {
		case SLE_FILE_I8:  x = (int8)SlReadByte(); break;
		case SLE_FILE_U8:  x = (byte)SlReadByte(); break;
		case SLE_FILE_I16: x = (int16)SlReadUint16(); break;
		case SLE_FILE_U16: x = (uint16)SlReadUint16(); break;
		case SLE_FILE_I32: x = (int32)SlReadUint32(); break;
		case SLE_FILE_U32: x = (uint32)SlReadUint32(); break;
		case SLE_FILE_I64: x = (int64)SlReadUint64(); break;
		case SLE_FILE_U64: x = (uint64)SlReadUint64(); break;
		case SLE_FILE_STRINGID: x = RemapOldStringID((uint16)SlReadUint16()); break;
		default: NOT_REACHED();
		}

		/* Write The value to the struct. These ARE endian safe. */
		switch ((conv >> 4) & 0xF) {
		case SLE_VAR_I8   >> 4:  *(int8*)ptr = x; break;
		case SLE_VAR_U8   >> 4:  *(byte*)ptr = x; break;
		case SLE_VAR_I16  >> 4: *(int16*)ptr = x; break;
		case SLE_VAR_U16  >> 4: *(uint16*)ptr = x; break;
		case SLE_VAR_I32  >> 4: *(int32*)ptr = x; break;
		case SLE_VAR_U32  >> 4: *(uint32*)ptr = x; break;
		case SLE_VAR_I64  >> 4: *(int64*)ptr = x; break;
		case SLE_VAR_U64  >> 4: *(uint64*)ptr = x; break;
		case SLE_VAR_NULL >> 4: break;
		default: NOT_REACHED();
		}
	}
}

/* Length in bytes of the various datatypes in a savefile. These
 * sizes are guaranteed by assert_compiles in stdafx.h */
static const byte _conv_lengths[] = {1, 1, 2, 2, 4, 4, 8, 8, 2};

/**
 * Return the size in bytes of a certain type of normal/atomic variable
 * @param var The variable the size is being asked of (NOTICE: unused)
 * @param conv @VarType type of variable that is used for calculating the size
 * @return Return the size of this type in byes
 */
static inline size_t SlCalcConvLen(const void *var, VarType conv) {return _conv_lengths[conv & 0xF];}

/**
 * Return the size in bytes of a reference (pointer)
 */
static inline size_t SlCalcRefLen(void) {return 2;}

/**
 * Return the size in bytes of a certain type of atomic array
 * @param array The variable the size is being asked of (NOTICE: unused)
 * @param length The length of the array counted in elements
 * @param conv @VarType type of the variable that is used in calculating the size
 */
static inline size_t SlCalcArrayLen(const void *array, uint length, VarType conv) {return _conv_lengths[conv & 0xF] * length;}

/**
 * Save/Load an array.
 * @param array The array being manipulated
 * @param length The length of the array in elements
 * @param conv @VarType type of the atomic array (int, byte, uint64, etc.)
 */
void SlArray(void *array, uint length, VarType conv)
{
	static const byte conv_mem_size[] = {1, 1, 2, 2, 4, 4, 8, 8, 0};

	// Automatically calculate the length?
	if (_sl.need_length != NL_NONE) {
		SlSetLength(SlCalcArrayLen(array, length, conv));
		// Determine length only?
		if (_sl.need_length == NL_CALCLENGTH)
			return;
	}

	/* NOTICE - handle some buggy stuff, in really old versions everything was saved
	 * as a byte-type. So detect this, and adjust array size accordingly */
	if (!_sl.save && _sl_version == 0) {
		if (conv == SLE_INT16 || conv == SLE_UINT16 || conv == SLE_STRINGID) {
			length *= 2; // int16, uint16 and StringID are 2 bytes in size
			conv = SLE_INT8;
		} else if (conv == SLE_INT32 || conv == SLE_UINT32) {
			length *= 4; // int32 and uint32 are 4 bytes in size
			conv = SLE_INT8;
		}
	}

	/* If the size of elements is 1 byte, no special conversion is needed,
	 * use specialized copy-to-copy function to speed up things */
	if (conv == SLE_INT8 || conv == SLE_UINT8) {
		SlCopyBytes(array, length);
	} else {
		byte *a = (byte*)array;
		for (; length != 0; length --) {
			SlSaveLoadConv(a, conv);
			a += conv_mem_size[(conv >> 4) & 0xF]; // get size
		}
	}
}

/**
 * Calculate the size of an object.
 * @param object Object that needs its length calculated
 * @param sld The @SaveLoad description of the object so we know how to manipulate it
 */
static size_t SlCalcObjLength(void *object, const SaveLoad *sld)
{
	size_t length = 0;

	// Need to determine the length and write a length tag.
	for (; sld->cmd != SL_END; sld++) {
		if (sld->cmd < SL_WRITEBYTE) {
			if (HASBIT(sld->cmd, 2)) {
				// check if the field is used in the current savegame version
				if (_sl_version < sld->version_from || _sl_version > sld->version_to)
					continue;
			}

			switch (sld->cmd) {
			case SL_VAR: case SL_CONDVAR: /* Normal Variable */
				length += SlCalcConvLen(NULL, sld->type); break;
			case SL_REF: case SL_CONDREF:  /* Reference variable */
				length += SlCalcRefLen(); break;
			case SL_ARR: case SL_CONDARR: /* Array */
				length += SlCalcArrayLen(NULL, sld->length, sld->type); break;
			default: NOT_REACHED();
			}
		} else if (sld->cmd == SL_WRITEBYTE) {
			length++; // a byte is logically of size 1
		} else if (sld->cmd == SL_INCLUDE) {
			length += SlCalcObjLength(NULL, _sl.includes[sld->version_from]);
		} else
			assert(sld->cmd == SL_END);
	}
	return length;
}

/**
 * Main SaveLoad function.
 * @param object The object that is being saved or loaded
 * @param sld The @SaveLoad description of the object so we know how to manipulate it
 */
void SlObject(void *object, const SaveLoad *sld)
{
	// Automatically calculate the length?
	if (_sl.need_length != NL_NONE) {
		SlSetLength(SlCalcObjLength(object, sld));
		if (_sl.need_length == NL_CALCLENGTH)
			return;
	}

	for (; sld->cmd != SL_END; sld++) {
		void *ptr = (byte*)object + sld->offset;

		if (sld->cmd < SL_WRITEBYTE) {
			/* CONDITIONAL saveload types depend on the savegame version */
			if (HASBIT(sld->cmd, 2)) {
				// check if the field is of the right version, if not, proceed to next one
				if (_sl_version < sld->version_from || _sl_version > sld->version_to)
					continue;
			}

			switch (sld->cmd) {
			case SL_VAR: case SL_CONDVAR: /* Normal variable */
				SlSaveLoadConv(ptr, sld->type); break;
			case SL_REF: case SL_CONDREF: /* Reference variable, translate */
				/// @todo XXX - another artificial limitof 65K elements of pointers?
				if (_sl.save) { // XXX - read/write pointer as uint16? What is with higher indeces?
					SlWriteUint16(_sl.ref_to_int_proc(*(void**)ptr, sld->type));
				} else
					*(void**)ptr = _sl.int_to_ref_proc(SlReadUint16(), sld->type);
				break;
			case SL_ARR: case SL_CONDARR: /* Array */
				SlArray(ptr, sld->length, sld->type); break;
			default: NOT_REACHED();
			}

		/* SL_WRITEBYTE translates a value of a variable to another one upon
		 * saving or loading.
		 * XXX - variable renaming abuse
		 * g_value: the value of the variable ingame is abused by sld->version_from
		 * f_value: the value of the variable in the savegame is abused by sld->version_to */
		} else if (sld->cmd == SL_WRITEBYTE) {
			if (_sl.save) {
				SlWriteByte(sld->version_to);
			} else
				*(byte*)ptr = sld->version_from;
		/* SL_INCLUDE loads common code for a type
		 * XXX - variable renaming abuse
		 * include_index: common code to include from _desc_includes[], abused by sld->version_from */
		} else if (sld->cmd == SL_INCLUDE) {
			SlObject(ptr, _sl.includes[sld->version_from]);
		} else
			assert(sld->cmd == SL_END);
	}
}

/** Calculate the length of global variables
 * @param desc The global variable that we want to know the size of
 * @return Returns the length of the sought global object
 */
static size_t SlCalcGlobListLength(const SaveLoadGlobVarList *desc)
{
	size_t length = 0;

	for (; desc->address != NULL; desc++) {
		// Of course the global variable must exist in the sought savegame version
		if (_sl_version >= desc->from_version && _sl_version <= desc->to_version)
			length += SlCalcConvLen(NULL, desc->conv);
	}
	return length;
}

/**
 * Save or Load (a list of) global variables
 * @param desc The global variable that is being loaded or saved
 */
void SlGlobList(const SaveLoadGlobVarList *desc)
{
	if (_sl.need_length != NL_NONE) {
		SlSetLength(SlCalcGlobListLength(desc));
		if (_sl.need_length == NL_CALCLENGTH)
			return;
	}

	for (; desc->address != NULL; desc++) {
		if (_sl_version >= desc->from_version && _sl_version <= desc->to_version)
			SlSaveLoadConv(desc->address, desc->conv);
	}
}

/**
 * Do something of which I have no idea what it is :P
 * @param proc The callback procedure that is called
 * @param arg The variable that will be used for the callback procedure
 */
void SlAutolength(AutolengthProc *proc, void *arg)
{
	uint32 offs;

	assert(_sl.save);

	// Tell it to calculate the length
	_sl.need_length = NL_CALCLENGTH;
	_sl.obj_len = 0;
	proc(arg);

	// Setup length
	_sl.need_length = NL_WANTLENGTH;
	SlSetLength(_sl.obj_len);

	offs = SlGetOffs() + _sl.obj_len;

	// And write the stuff
	proc(arg);

	assert(offs == SlGetOffs());
}

/**
 * Load a chunk of data (eg vehicles, stations, etc.)
 * @param ch The chunkhandler that will be used for the operation
 */
static void SlLoadChunk(const ChunkHandler *ch)
{
	byte m = SlReadByte();
	size_t len;
	uint32 endoffs;

	_sl.block_mode = m;
	_sl.obj_len = 0;

	switch (m) {
	case CH_ARRAY:
		_sl.array_index = 0;
		ch->load_proc();
		break;
	case CH_SPARSE_ARRAY:
		ch->load_proc();
		break;
	default:
		if ((m & 0xF) == CH_RIFF) {
			// Read length
			len = (SlReadByte() << 16) | ((m >> 4) << 24);
			len += SlReadUint16();
			_sl.obj_len = len;
			endoffs = SlGetOffs() + len;
			ch->load_proc();
			assert(SlGetOffs() == endoffs);
		} else {
			SlError("Invalid chunk type");
		}
		break;
	}
}

/* Stub Chunk handlers to only calculate length and do nothing else */
static ChunkSaveLoadProc *_tmp_proc_1;
static inline void SlStubSaveProc2(void *arg) {_tmp_proc_1();}
static void SlStubSaveProc(void) {SlAutolength(SlStubSaveProc2, NULL);}

/** Save a chunk of data (eg. vehicles, stations, etc.). Each chunk is
 * prefixed by an ID identifying it, followed by data, and terminator where appropiate
 * @param ch The chunkhandler that will be used for the operation
 */
static void SlSaveChunk(const ChunkHandler *ch)
{
	ChunkSaveLoadProc *proc = ch->save_proc;

	SlWriteUint32(ch->id);

	if (ch->flags & CH_AUTO_LENGTH) {
		// Need to calculate the length. Solve that by calling SlAutoLength in the save_proc.
		_tmp_proc_1 = proc;
		proc = SlStubSaveProc;
	}

	_sl.block_mode = ch->flags & CH_TYPE_MASK;
	switch (ch->flags & CH_TYPE_MASK) {
	case CH_RIFF:
		_sl.need_length = NL_WANTLENGTH;
		proc();
		break;
	case CH_ARRAY:
		_sl.last_array_index = 0;
		SlWriteByte(CH_ARRAY);
		proc();
		SlWriteArrayLength(0); // Terminate arrays
		break;
	case CH_SPARSE_ARRAY:
		SlWriteByte(CH_SPARSE_ARRAY);
		proc();
		SlWriteArrayLength(0); // Terminate arrays
		break;
	default: NOT_REACHED();
	}
}

/** Save all chunks */
static void SlSaveChunks(void)
{
	const ChunkHandler *ch;
	const ChunkHandler* const *chsc;
	uint p;

	for (p = 0; p != CH_NUM_PRI_LEVELS; p++) {
		for (chsc = _sl.chs; (ch = *chsc++) != NULL;) {
			while (true) {
				if (((ch->flags >> CH_PRI_SHL) & (CH_NUM_PRI_LEVELS - 1)) == p)
					SlSaveChunk(ch);
				if (ch->flags & CH_LAST)
					break;
				ch++;
			}
		}
	}

	// Terminator
	SlWriteUint32(0);
}

/** Find the ChunkHandler that will be used for processing the found
 * chunk in the savegame or in memory
 * @param id the chunk in question
 * @return returns the appropiate chunkhandler
 */
static const ChunkHandler *SlFindChunkHandler(uint32 id)
{
	const ChunkHandler *ch;
	const ChunkHandler *const *chsc;
	for (chsc = _sl.chs; (ch=*chsc++) != NULL;) {
		while(true) {
			if (ch->id == id)
				return ch;
			if (ch->flags & CH_LAST)
				break;
			ch++;
		}
	}
	return NULL;
}

/** Load all chunks */
static void SlLoadChunks(void)
{
	uint32 id;
	const ChunkHandler *ch;

	for (id = SlReadUint32(); id != 0; id = SlReadUint32()) {
		DEBUG(misc, 1) ("Loading chunk %c%c%c%c", id >> 24, id>>16, id>>8, id);

		ch = SlFindChunkHandler(id);
		if (ch == NULL) SlError("found unknown tag in savegame (sync error)");
		SlLoadChunk(ch);
	}
}

//*******************************************
//********** START OF LZO CODE **************
//*******************************************
#define LZO_SIZE 8192

#include "minilzo.h"

static uint ReadLZO(void)
{
	byte out[LZO_SIZE + LZO_SIZE / 64 + 16 + 3 + 8];
	uint32 tmp[2];
	uint32 size;
	uint len;

	// Read header
	if (fread(tmp, sizeof(tmp), 1, _sl.fh) != 1) SlError("file read failed");

	// Check if size is bad
	((uint32*)out)[0] = size = tmp[1];

	if (_sl_version != 0) {
		tmp[0] = TO_BE32(tmp[0]);
		size = TO_BE32(size);
	}

	if (size >= sizeof(out)) SlError("inconsistent size");

	// Read block
	if (fread(out + sizeof(uint32), size, 1, _sl.fh) != 1) SlError("file read failed");

	// Verify checksum
	if (tmp[0] != lzo_adler32(0, out, size + sizeof(uint32))) SlError("bad checksum");

	// Decompress
	lzo1x_decompress(out + sizeof(uint32)*1, size, _sl.buf, &len, NULL);
	return len;
}

// p contains the pointer to the buffer, len contains the pointer to the length.
// len bytes will be written, p and l will be updated to reflect the next buffer.
static void WriteLZO(uint size)
{
	byte out[LZO_SIZE + LZO_SIZE / 64 + 16 + 3 + 8];
	byte wrkmem[sizeof(byte*)*4096];
	uint outlen;

	lzo1x_1_compress(_sl.buf, size, out + sizeof(uint32)*2, &outlen, wrkmem);
	((uint32*)out)[1] = TO_BE32(outlen);
	((uint32*)out)[0] = TO_BE32(lzo_adler32(0, out + sizeof(uint32), outlen + sizeof(uint32)));
	if (fwrite(out, outlen + sizeof(uint32)*2, 1, _sl.fh) != 1) SlError("file write failed");
}

static bool InitLZO(void)
{
	_sl.bufsize = LZO_SIZE;
	_sl.buf = _sl.buf_ori = (byte*)malloc(LZO_SIZE);
	return true;
}

static void UninitLZO(void)
{
	free(_sl.buf_ori);
}

//*********************************************
//******** START OF NOCOMP CODE (uncompressed)*
//*********************************************
static uint ReadNoComp(void)
{
	return fread(_sl.buf, 1, LZO_SIZE, _sl.fh);
}

static void WriteNoComp(uint size)
{
	fwrite(_sl.buf, 1, size, _sl.fh);
}

static bool InitNoComp(void)
{
	_sl.bufsize = LZO_SIZE;
	_sl.buf = _sl.buf_ori =(byte*)malloc(LZO_SIZE);
	return true;
}

static void UninitNoComp(void)
{
	free(_sl.buf_ori);
}

//********************************************
//********** START OF MEMORY CODE (in ram)****
//********************************************

enum {
	SAVE_POOL_BLOCK_SIZE_BITS = 17,
	SAVE_POOL_MAX_BLOCKS = 500
};

#include "network.h"
#include "table/strings.h"
#include "table/sprites.h"
#include "gfx.h"
#include "gui.h"

typedef struct ThreadedSave {
	MemoryPool *save;
	uint count;
	bool ff_state;
	bool saveinprogress;
	CursorID cursor;
} ThreadedSave;

/* A maximum size of of 128K * 500 = 64.000KB savegames */
static MemoryPool _save_pool = {"Savegame", SAVE_POOL_MAX_BLOCKS, SAVE_POOL_BLOCK_SIZE_BITS, sizeof(byte), NULL, 0, 0, NULL};
static ThreadedSave _ts;

static bool InitMem(void)
{
	_ts.save = &_save_pool;
	_ts.count = 0;

	CleanPool(_ts.save);
	AddBlockToPool(_ts.save);

	/* A block from the pool is a contigious area of memory, so it is safe to write to it sequentially */
	_sl.bufsize = _ts.save->total_items;
	_sl.buf = (byte*)GetItemFromPool(_ts.save, _ts.count);
	return true;
}

static void UnInitMem(void)
{
	CleanPool(_ts.save);
	_ts.save = NULL;
}

static void WriteMem(uint size)
{
	_ts.count += size;
	/* Allocate new block and new buffer-pointer */
	AddBlockIfNeeded(_ts.save, _ts.count);
	_sl.buf = (byte*)GetItemFromPool(_ts.save, _ts.count);
}

//********************************************
//********** START OF ZLIB CODE **************
//********************************************

#if defined(WITH_ZLIB)
#include <zlib.h>

static z_stream _z;

static bool InitReadZlib(void)
{
	memset(&_z, 0, sizeof(_z));
	if (inflateInit(&_z) != Z_OK) return false;

	_sl.bufsize = 4096;
	_sl.buf = _sl.buf_ori = (byte*)malloc(4096 + 4096); // also contains fread buffer
	return true;
}

static uint ReadZlib(void)
{
	int r;

	_z.next_out = _sl.buf;
	_z.avail_out = 4096;

	do {
		// read more bytes from the file?
		if (_z.avail_in == 0) {
			_z.avail_in = fread(_z.next_in = _sl.buf + 4096, 1, 4096, _sl.fh);
		}

		// inflate the data
		r = inflate(&_z, 0);
		if (r == Z_STREAM_END)
			break;

		if (r != Z_OK)
			SlError("inflate() failed");
	} while (_z.avail_out);

	return 4096 - _z.avail_out;
}

static void UninitReadZlib(void)
{
	inflateEnd(&_z);
	free(_sl.buf_ori);
}

static bool InitWriteZlib(void)
{
	memset(&_z, 0, sizeof(_z));
	if (deflateInit(&_z, 6) != Z_OK) return false;

	_sl.bufsize = 4096;
	_sl.buf = _sl.buf_ori = (byte*)malloc(4096); // also contains fread buffer
	return true;
}

static void WriteZlibLoop(z_streamp z, byte *p, uint len, int mode)
{
	byte buf[1024]; // output buffer
	int r;
	uint n;
	z->next_in = p;
	z->avail_in = len;
	do {
		z->next_out = buf;
		z->avail_out = sizeof(buf);
		r = deflate(z, mode);
			// bytes were emitted?
		if ((n=sizeof(buf) - z->avail_out) != 0) {
			if (fwrite(buf, n, 1, _sl.fh) != 1) SlError("file write error");
		}
		if (r == Z_STREAM_END)
			break;
		if (r != Z_OK) SlError("zlib returned error code");
	} while (z->avail_in || !z->avail_out);
}

static void WriteZlib(uint len)
{
	WriteZlibLoop(&_z, _sl.buf, len, 0);
}

static void UninitWriteZlib(void)
{
	// flush any pending output.
	if (_sl.fh) WriteZlibLoop(&_z, NULL, 0, Z_FINISH);
	deflateEnd(&_z);
	free(_sl.buf_ori);
}

#endif /* WITH_ZLIB */

//*******************************************
//************* END OF CODE *****************
//*******************************************

// these define the chunks
extern const ChunkHandler _misc_chunk_handlers[];
extern const ChunkHandler _player_chunk_handlers[];
extern const ChunkHandler _veh_chunk_handlers[];
extern const ChunkHandler _waypoint_chunk_handlers[];
extern const ChunkHandler _depot_chunk_handlers[];
extern const ChunkHandler _order_chunk_handlers[];
extern const ChunkHandler _town_chunk_handlers[];
extern const ChunkHandler _sign_chunk_handlers[];
extern const ChunkHandler _station_chunk_handlers[];
extern const ChunkHandler _industry_chunk_handlers[];
extern const ChunkHandler _engine_chunk_handlers[];
extern const ChunkHandler _economy_chunk_handlers[];
extern const ChunkHandler _animated_tile_chunk_handlers[];

static const ChunkHandler * const _chunk_handlers[] = {
	_misc_chunk_handlers,
	_veh_chunk_handlers,
	_waypoint_chunk_handlers,
	_depot_chunk_handlers,
	_order_chunk_handlers,
	_industry_chunk_handlers,
	_economy_chunk_handlers,
	_engine_chunk_handlers,
	_town_chunk_handlers,
	_sign_chunk_handlers,
	_station_chunk_handlers,
	_player_chunk_handlers,
	_animated_tile_chunk_handlers,
	NULL,
};

// used to include a vehicle desc in another desc.
extern const SaveLoad _common_veh_desc[];
static const SaveLoad* const _desc_includes[] = {
	_common_veh_desc
};

/**
 * Pointers cannot be saved to a savegame, so this functions gets
 * the index of the item, and if not available, it hussles with
 * pointers (looks really bad :()
 * Remember that a NULL item has value 0, and all
 * indeces have +1, so vehicle 0 is saved as index 1.
 * @param obj The object that we want to get the index of
 * @param rt @SLRefType type of the object the index is being sought of
 * @return Return the pointer converted to an index of the type pointed to
 */
static uint ReferenceToInt(const void *obj, SLRefType rt)
{
	if (obj == NULL) return 0;

	switch (rt) {
		case REF_VEHICLE_OLD: // Old vehicles we save as new onces
		case REF_VEHICLE:   return ((const  Vehicle*)obj)->index + 1;
		case REF_STATION:   return ((const  Station*)obj)->index + 1;
		case REF_TOWN:      return ((const     Town*)obj)->index + 1;
		case REF_ORDER:     return ((const    Order*)obj)->index + 1;
		case REF_ROADSTOPS: return ((const RoadStop*)obj)->index + 1;
		default: NOT_REACHED();
	}

	return 0; // avoid compiler warning
}

/**
 * Pointers cannot be loaded from a savegame, so this function
 * gets the index from the savegame and returns the appropiate
 * pointer from the already loaded base.
 * Remember that an index of 0 is a NULL pointer so all indeces
 * are +1 so vehicle 0 is saved as 1.
 * @param index The index that is being converted to a pointer
 * @param rt @SLRefType type of the object the pointer is sought of
 * @return Return the index converted to a pointer of any type
 */
static void *IntToReference(uint index, SLRefType rt)
{
	/* After version 4.3 REF_VEHICLE_OLD is saved as REF_VEHICLE,
	 * and should be loaded like that */
	if (rt == REF_VEHICLE_OLD && !CheckSavegameVersionOldStyle(4, 4))
		rt = REF_VEHICLE;

	/* No need to look up NULL pointers, just return immediately */
	if (rt != REF_VEHICLE_OLD && index == 0)
		return NULL;

	index--; // correct for the NULL index

	switch (rt) {
		case REF_ORDER: {
			if (!AddBlockIfNeeded(&_order_pool, index))
				error("Orders: failed loading savegame: too many orders");
			return GetOrder(index);
		}
		case REF_VEHICLE: {
			if (!AddBlockIfNeeded(&_vehicle_pool, index))
				error("Vehicles: failed loading savegame: too many vehicles");
			return GetVehicle(index);
		}
		case REF_STATION: {
			if (!AddBlockIfNeeded(&_station_pool, index))
				error("Stations: failed loading savegame: too many stations");
			return GetStation(index);
		}
		case REF_TOWN: {
			if (!AddBlockIfNeeded(&_town_pool, index))
				error("Towns: failed loading savegame: too many towns");
			return GetTown(index);
		}
		case REF_ROADSTOPS: {
			if (!AddBlockIfNeeded(&_roadstop_pool, index))
				error("RoadStops: failed loading savegame: too many RoadStops");
			return GetRoadStop(index);
		}

		case REF_VEHICLE_OLD: {
			/* Old vehicles were saved differently:
			 * invalid vehicle was 0xFFFF,
			 * and the index was not - 1.. correct for this */
			index++;
			if (index == INVALID_VEHICLE)
				return NULL;

			if (!AddBlockIfNeeded(&_vehicle_pool, index))
				error("Vehicles: failed loading savegame: too many vehicles");
			return GetVehicle(index);
		}
		default: NOT_REACHED();
	}

	return NULL;
}

/** The format for a reader/writer type of a savegame */
typedef struct {
	const char *name;           /// name of the compressor/decompressor (debug-only)
	uint32 tag;                 /// the 4-letter tag by which it is identified in the savegame

	bool (*init_read)(void);    /// function executed upon initalization of the loader
	ReaderProc *reader;         /// function that loads the data from the file
	void (*uninit_read)(void);  /// function executed when reading is finished

	bool (*init_write)(void);   /// function executed upon intialization of the saver
	WriterProc *writer;         /// function that saves the data to the file
	void (*uninit_write)(void); /// function executed when writing is done
} SaveLoadFormat;

static const SaveLoadFormat _saveload_formats[] = {
	{"memory", 0,                NULL,         NULL,       NULL,           InitMem,       WriteMem,    UnInitMem},
	{"lzo",    TO_BE32X('OTTD'), InitLZO,      ReadLZO,    UninitLZO,      InitLZO,       WriteLZO,    UninitLZO},
	{"none",   TO_BE32X('OTTN'), InitNoComp,   ReadNoComp, UninitNoComp,   InitNoComp,    WriteNoComp, UninitNoComp},
#if defined(WITH_ZLIB)
	{"zlib",   TO_BE32X('OTTZ'), InitReadZlib, ReadZlib,   UninitReadZlib, InitWriteZlib, WriteZlib,   UninitWriteZlib},
#else
	{"zlib",   TO_BE32X('OTTZ'), NULL,         NULL,       NULL,           NULL,          NULL,        NULL},
#endif
};

/**
 * Return the savegameformat of the game. Whether it was create with ZLIB compression
 * uncompressed, or another type
 * @param s Name of the savegame format. If NULL it picks the first available one
 * @return Pointer to @SaveLoadFormat struct giving all characteristics of this type of savegame
 */
static const SaveLoadFormat *GetSavegameFormat(const char *s)
{
	const SaveLoadFormat *def = endof(_saveload_formats) - 1;

	// find default savegame format, the highest one with which files can be written
	while (!def->init_write) def--;

	if (s != NULL && s[0] != '\0') {
		const SaveLoadFormat *slf;
		for (slf = &_saveload_formats[0]; slf != endof(_saveload_formats); slf++) {
			if (slf->init_write != NULL && strcmp(s, slf->name) == 0)
				return slf;
		}

		ShowInfoF("Savegame format '%s' is not available. Reverting to '%s'.", s, def->name);
	}
	return def;
}

// actual loader/saver function
void InitializeGame(uint size_x, uint size_y);
extern bool AfterLoadGame(uint version);
extern void BeforeSaveGame(void);
extern bool LoadOldSaveGame(const char *file);

/** Small helper function to close the to be loaded savegame an signal error */
static inline SaveOrLoadResult AbortSaveLoad(void)
{
	if (_sl.fh != NULL) fclose(_sl.fh);

	_sl.fh = NULL;
	return SL_ERROR;
}

/** Update the gui accordingly when starting saving
 * and set locks on saveload. Also turn off fast-forward cause with that
 * saving takes Aaaaages */
void SaveFileStart(void)
{
	_ts.ff_state = _fast_forward;
	_fast_forward = false;
	if (_cursor.sprite == SPR_CURSOR_MOUSE) SetMouseCursor(SPR_CURSOR_ZZZ);

	SendWindowMessage(WC_STATUS_BAR, 0, true, 0, 0);
	_ts.saveinprogress = true;
}

/** Update the gui accordingly when saving is done and release locks
 * on saveload */
void SaveFileDone(void)
{
	_fast_forward = _ts.ff_state;
	if (_cursor.sprite == SPR_CURSOR_ZZZ) SetMouseCursor(SPR_CURSOR_MOUSE);

	SendWindowMessage(WC_STATUS_BAR, 0, false, 0, 0);
	_ts.saveinprogress = false;
}

/** Show a gui message when saving has failed */
void SaveFileError(void)
{
	ShowErrorMessage(STR_4007_GAME_SAVE_FAILED, STR_NULL, 0, 0);
	SaveFileDone();
}

static Thread* save_thread;

/** We have written the whole game into memory, _save_pool, now find
 * and appropiate compressor and start writing to file.
 */
static void* SaveFileToDisk(void *arg)
{
	const SaveLoadFormat *fmt = GetSavegameFormat(_savegame_format);
	uint32 hdr[2];

	if (arg != NULL) OTTD_SendThreadMessage(MSG_OTTD_SAVETHREAD_START);

	/* XXX - Setup setjmp error handler if an error occurs anywhere deep during
	 * loading/saving execute a longjmp() and continue execution here */
	if (setjmp(_sl.excpt)) {
		AbortSaveLoad();
		_sl.excpt_uninit();

		fprintf(stderr, "Save game failed: %s.", _sl.excpt_msg);
		if (arg != NULL) OTTD_SendThreadMessage(MSG_OTTD_SAVETHREAD_ERROR);
		else SaveFileError();
		return NULL;
	}

	/* We have written our stuff to memory, now write it to file! */
	hdr[0] = fmt->tag;
	hdr[1] = TO_BE32(SAVEGAME_VERSION << 16);
	if (fwrite(hdr, sizeof(hdr), 1, _sl.fh) != 1) SlError("file write failed");

	if (!fmt->init_write()) SlError("cannot initialize compressor");

	{
		uint i;
		uint count = 1 << _ts.save->block_size_bits;

		assert(_ts.count == _sl.offs_base);
		for (i = 0; i != _ts.save->current_blocks - 1; i++) {
			_sl.buf = _ts.save->blocks[i];
			fmt->writer(count);
		}

		/* The last block is (almost) always not fully filled, so only write away
		 * as much data as it is in there */
		_sl.buf = _ts.save->blocks[i];
		fmt->writer(_ts.count - (i * count));
	}

	fmt->uninit_write();
	assert(_ts.count == _sl.offs_base);
	GetSavegameFormat("memory")->uninit_write(); // clean the memorypool
	fclose(_sl.fh);

	if (arg != NULL) OTTD_SendThreadMessage(MSG_OTTD_SAVETHREAD_DONE);
	return NULL;
}

void WaitTillSaved(void)
{
	OTTDJoinThread(save_thread);
	save_thread = NULL;
}


/**
 * Main Save or Load function where the high-level saveload functions are
 * handled. It opens the savegame, selects format and checks versions
 * @param filename The name of the savegame being created/loaded
 * @param mode Save or load. Load can also be a TTD(Patch) game. Use SL_LOAD, SL_OLD_LOAD or SL_SAVE
 * @return Return the results of the action. SL_OK, SL_ERROR or SL_REINIT ("unload" the game)
 */
SaveOrLoadResult SaveOrLoad(const char *filename, int mode)
{
	uint32 hdr[2];
	const SaveLoadFormat *fmt;
	uint version;

	/* An instance of saving is already active, so don't go saving again */
	if (_ts.saveinprogress && mode == SL_SAVE) {
		// if not an autosave, but a user action, show error message
		if (!_do_autosave) ShowErrorMessage(INVALID_STRING_ID, STR_SAVE_STILL_IN_PROGRESS, 0, 0);
		return SL_OK;
	}
	WaitTillSaved();

	/* Load a TTDLX or TTDPatch game */
	if (mode == SL_OLD_LOAD) {
		InitializeGame(256, 256); // set a mapsize of 256x256 for TTDPatch games or it might get confused
		if (!LoadOldSaveGame(filename)) return SL_REINIT;
		AfterLoadGame(0);
		return SL_OK;
	}

	_sl.fh = fopen(filename, (mode == SL_SAVE) ? "wb" : "rb");
	if (_sl.fh == NULL) {
		DEBUG(misc, 0) ("Cannot open savegame for saving/loading.");
		return SL_ERROR;
	}

	_sl.bufe = _sl.bufp = NULL;
	_sl.offs_base = 0;
	_sl.int_to_ref_proc = IntToReference;
	_sl.ref_to_int_proc = ReferenceToInt;
	_sl.save = mode;
	_sl.includes = _desc_includes;
	_sl.chs = _chunk_handlers;

	/* XXX - Setup setjmp error handler if an error occurs anywhere deep during
	 * loading/saving execute a longjmp() and continue execution here */
	if (setjmp(_sl.excpt)) {
		AbortSaveLoad();

		// deinitialize compressor.
		_sl.excpt_uninit();

		/* A saver/loader exception!! reinitialize all variables to prevent crash! */
		if (mode == SL_LOAD) {
			ShowInfoF("Load game failed: %s.", _sl.excpt_msg);
			return SL_REINIT;
		}

		ShowInfoF("Save game failed: %s.", _sl.excpt_msg);
		return SL_ERROR;
	}

	/* We first initialize here to avoid: "warning: variable `version' might
	 * be clobbered by `longjmp' or `vfork'" */
	version = 0;

	/* General tactic is to first save the game to memory, then use an available writer
	 * to write it to file, either in threaded mode if possible, or single-threaded */
	if (mode == SL_SAVE) { /* SAVE game */
		fmt = GetSavegameFormat("memory"); // write to memory

		_sl.write_bytes = fmt->writer;
		_sl.excpt_uninit = fmt->uninit_write;
		if (!fmt->init_write()) {
			DEBUG(misc, 0) ("Initializing writer %s failed.", fmt->name);
			return AbortSaveLoad();
		}

		_sl_version = SAVEGAME_VERSION;

		BeforeSaveGame();
		SlSaveChunks();
		SlWriteFill(); // flush the save buffer

		if (_network_server ||
					(save_thread = OTTDCreateThread(&SaveFileToDisk, (void*)"")) == NULL) {
			DEBUG(misc, 1) ("cannot create savegame thread, reverting to single-threaded mode...");
			SaveFileToDisk(NULL);
		}

	} else { /* LOAD game */
		assert(mode == SL_LOAD);

		if (fread(hdr, sizeof(hdr), 1, _sl.fh) != 1) {
			DEBUG(misc, 0) ("Cannot read savegame header, aborting.");
			return AbortSaveLoad();
		}

		// see if we have any loader for this type.
		for (fmt = _saveload_formats; ; fmt++) {
			/* No loader found, treat as version 0 and use LZO format */
			if (fmt == endof(_saveload_formats)) {
				DEBUG(misc, 0) ("Unknown savegame type, trying to load it as the buggy format.");
				rewind(_sl.fh);
				_sl_version = version = 0;
				_sl_minor_version = 0;
				fmt = _saveload_formats + 1; // LZO
				break;
			}

			if (fmt->tag == hdr[0]) {
				// check version number
				_sl_version = version = TO_BE32(hdr[1]) >> 16;
				/* Minor is not used anymore from version 18.0, but it is still needed
				 *  in versions before that (4 cases) which can't be removed easy.
				 *  Therefor it is loaded, but never saved (or, it saves a 0 in any scenario).
				 *  So never EVER use this minor version again. -- TrueLight -- 22-11-2005 */
				_sl_minor_version = (TO_BE32(hdr[1]) >> 8) & 0xFF;

				DEBUG(misc, 1)("[Savegame] Loading savegame version %d\n", _sl_version);

				/* Is the version higher than the current? */
				if (_sl_version > SAVEGAME_VERSION) {
					DEBUG(misc, 0) ("Savegame version invalid.");
					return AbortSaveLoad();
				}
				break;
			}
		}

		_sl.read_bytes = fmt->reader;
		_sl.excpt_uninit = fmt->uninit_read;

		// loader for this savegame type is not implemented?
		if (fmt->init_read == NULL) {
			ShowInfoF("Loader for '%s' is not available.", fmt->name);
			return AbortSaveLoad();
		}

		if (!fmt->init_read()) {
			DEBUG(misc, 0) ("Initializing loader %s failed.", fmt->name);
			return AbortSaveLoad();
		}

		/* Old maps were hardcoded to 256x256 and thus did not contain
		 * any mapsize information. Pre-initialize to 256x256 to not to
		 * confuse old games */
		InitializeGame(256, 256);

		SlLoadChunks();
		fmt->uninit_read();
		fclose(_sl.fh);

		/* After loading fix up savegame for any internal changes that
		* might've occured since then. If it fails, load back the old game */
		if (!AfterLoadGame(version)) return SL_REINIT;
	}

	return SL_OK;
}

/** Do a save when exiting the game (patch option) _patches.autosave_on_exit */
void DoExitSave(void)
{
	char buf[200];
	snprintf(buf, sizeof(buf), "%s%sexit.sav", _path.autosave_dir, PATHSEP);
	SaveOrLoad(buf, SL_SAVE);
}

#if 0
/**
 * Function to get the type of the savegame by looking at the file header.
 * NOTICE: Not used right now, but could be used if extensions of savegames are garbled
 * @param file Savegame to be checked
 * @return SL_OLD_LOAD or SL_LOAD of the file
 */
int GetSavegameType(char *file)
{
	const SaveLoadFormat *fmt;
	uint32 hdr;
	FILE *f;
	int mode = SL_OLD_LOAD;

	f = fopen(file, "rb");
	if (fread(&hdr, sizeof(hdr), 1, f) != 1) {
		printf("Savegame is obsolete or invalid format.\n");
		mode = SL_LOAD; // don't try to get filename, just show name as it is written
	}
	else {
		// see if we have any loader for this type.
		for (fmt = _saveload_formats; fmt != endof(_saveload_formats); fmt++) {
			if (fmt->tag == hdr) {
				mode = SL_LOAD; // new type of savegame
				break;
			}
		}
	}

	fclose(f);
	return mode;
}
#endif
