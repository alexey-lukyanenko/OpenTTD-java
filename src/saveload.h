/* $Id: saveload.h 3227 2005-11-22 19:33:29Z truelight $ */

#ifndef SAVELOAD_H
#define SAVELOAD_H

typedef enum SaveOrLoadResult {
	SL_OK = 0, // completed successfully
	SL_ERROR = 1, // error that was caught before internal structures were modified
	SL_REINIT = 2, // error that was caught in the middle of updating game state, need to clear it. (can only happen during load)
} SaveOrLoadResult;

typedef enum SaveOrLoadMode {
	SL_INVALID = -1,
	SL_LOAD = 0,
	SL_SAVE = 1,
	SL_OLD_LOAD = 2,
} SaveOrLoadMode;

SaveOrLoadResult SaveOrLoad(const char *filename, int mode);
void WaitTillSaved(void);


typedef void ChunkSaveLoadProc(void);
typedef void AutolengthProc(void *arg);

typedef struct SaveLoadGlobVarList {
	void *address;
	byte conv;
	uint16 from_version;
	uint16 to_version;
} SaveLoadGlobVarList;

typedef struct {
	uint32 id;
	ChunkSaveLoadProc *save_proc;
	ChunkSaveLoadProc *load_proc;
	uint32 flags;
} ChunkHandler;

typedef struct {
	byte null;
} NullStruct;

typedef enum SLRefType {
	REF_ORDER       = 0,
	REF_VEHICLE     = 1,
	REF_STATION     = 2,
	REF_TOWN        = 3,
	REF_VEHICLE_OLD = 4,
	REF_ROADSTOPS   = 5
} SLRefType;


extern uint16 _sl_version;       /// the major savegame version identifier
extern byte   _sl_minor_version; /// the minor savegame version, DO NOT USE!


enum {
	INC_VEHICLE_COMMON = 0,
};

enum {
	CH_RIFF = 0,
	CH_ARRAY = 1,
	CH_SPARSE_ARRAY = 2,
	CH_TYPE_MASK = 3,
	CH_LAST = 8,
	CH_AUTO_LENGTH = 16,
	CH_PRI_0 = 0 << 4,
	CH_PRI_1 = 1 << 4,
	CH_PRI_2 = 2 << 4,
	CH_PRI_3 = 3 << 4,
	CH_PRI_SHL = 4,
	CH_NUM_PRI_LEVELS = 4,
};

typedef enum VarTypes {
	SLE_FILE_I8  = 0,
	SLE_FILE_U8  = 1,
	SLE_FILE_I16 = 2,
	SLE_FILE_U16 = 3,
	SLE_FILE_I32 = 4,
	SLE_FILE_U32 = 5,
	SLE_FILE_I64 = 6,
	SLE_FILE_U64 = 7,

	SLE_FILE_STRINGID = 8,
//	SLE_FILE_IVAR = 8,
//	SLE_FILE_UVAR = 9,

	SLE_VAR_I8   = 0 << 4,
	SLE_VAR_U8   = 1 << 4,
	SLE_VAR_I16  = 2 << 4,
	SLE_VAR_U16  = 3 << 4,
	SLE_VAR_I32  = 4 << 4,
	SLE_VAR_U32  = 5 << 4,
	SLE_VAR_I64  = 6 << 4,
	SLE_VAR_U64  = 7 << 4,

	SLE_VAR_NULL = 8 << 4, // useful to write zeros in savegame.

	SLE_VAR_INT  = SLE_VAR_I32,
	SLE_VAR_UINT = SLE_VAR_U32,

	SLE_INT8   = SLE_FILE_I8  | SLE_VAR_I8,
	SLE_UINT8  = SLE_FILE_U8  | SLE_VAR_U8,
	SLE_INT16  = SLE_FILE_I16 | SLE_VAR_I16,
	SLE_UINT16 = SLE_FILE_U16 | SLE_VAR_U16,
	SLE_INT32  = SLE_FILE_I32 | SLE_VAR_I32,
	SLE_UINT32 = SLE_FILE_U32 | SLE_VAR_U32,
	SLE_INT64  = SLE_FILE_I64 | SLE_VAR_I64,
	SLE_UINT64 = SLE_FILE_U64 | SLE_VAR_U64,

	SLE_STRINGID = SLE_FILE_STRINGID | SLE_VAR_U16,
} VarType;

enum SaveLoadTypes {
	SL_VAR       = 0,
	SL_REF       = 1,
	SL_ARR       = 2,
	SL_CONDVAR   = 0 | (1 << 2), // 4
	SL_CONDREF   = 1 | (1 << 2), // 5
	SL_CONDARR   = 2 | (1 << 2), // 6
	// non-normal save-load types
	SL_WRITEBYTE = 8,
	SL_INCLUDE   = 9,
	SL_END       = 15
};

/** SaveLoad type struct. Do NOT use this directly but use the SLE_ macros defined just below! */
typedef struct SaveLoad {
	byte cmd;             /// the action to take with the saved/loaded type, All types need different action
	VarType type;         /// type of the variable to be saved, int
	uint16 offset;        /// offset of this variable in the struct (max offset is 65536)
	uint16 length;        /// (conditional) length of the variable (eg. arrays) (max array size is 65536 elements)
	uint16 version_from;  /// save/load the variable starting from this savegame version
	uint16 version_to;    /// save/load the variable until this savegame version
} SaveLoad;

/* Simple variables, references (pointers) and arrays */
#define SLE_VAR(base, variable, type) {SL_VAR, type, offsetof(base, variable), 0, 0, 0}
#define SLE_REF(base, variable, type) {SL_REF, type, offsetof(base, variable), 0, 0, 0}
#define SLE_ARR(base, variable, type, length) {SL_ARR, type, offsetof(base, variable), length, 0, 0}
/* Conditional variables, references (pointers) and arrays that are only valid for certain savegame versions */
#define SLE_CONDVAR(base, variable, type, from, to) {SL_CONDVAR, type, offsetof(base, variable), 0, from, to}
#define SLE_CONDREF(base, variable, type, from, to) {SL_CONDREF, type, offsetof(base, variable), 0, from, to}
#define SLE_CONDARR(base, variable, type, length, from, to) {SL_CONDARR, type, offsetof(base, variable), length, from, to}
/* Translate values ingame to different values in the savegame and vv */
#define SLE_WRITEBYTE(base, variable, game_value, file_value) {SL_WRITEBYTE, 0, offsetof(base, variable), 0, game_value, file_value}
/* Load common code and put it into each struct (currently only for vehicles */
#define SLE_INCLUDE(base, variable, include_index) {SL_INCLUDE, 0, offsetof(base, variable), 0, include_index, 0}

/* The same as the ones at the top, only the offset is given directly; used for unions */
#define SLE_VARX(offset, type) {SL_VAR, type, offset, 0, 0, 0}
#define SLE_REFX(offset, type) {SL_REF, type, offset, 0, 0, 0}
#define SLE_CONDVARX(offset, type, from, to) {SL_CONDVAR, type, offset, 0, from, to}
#define SLE_CONDREFX(offset, type, from, to) {SL_CONDREF, type, offset, 0, from, to}
#define SLE_WRITEBYTEX(offset, something) {SL_WRITEBYTE, 0, offset, 0, something, 0}
#define SLE_INCLUDEX(offset, type) {SL_INCLUDE, type, offset, 0, 0, 0}

/* End marker */
#define SLE_END() {SL_END, 0, 0, 0, 0, 0}

/** Checks if the savegame is below major.minor.
 */
static inline bool CheckSavegameVersionOldStyle(uint16 major, byte minor)
{
	return (_sl_version < major) || (_sl_version == major && _sl_minor_version < minor);
}

/** Checks if the savegame is below version.
 */
static inline bool CheckSavegameVersion(uint16 version)
{
	return _sl_version < version;
}

void SlSetArrayIndex(uint index);
int SlIterateArray(void);
void SlArray(void *array, uint length, VarType conv);
void SlObject(void *object, const SaveLoad *desc);
void SlAutolength(AutolengthProc *proc, void *arg);
uint SlGetFieldLength(void);
int SlReadByte(void);
void SlSetLength(size_t length);
void SlWriteByte(byte b);
void SlGlobList(const SaveLoadGlobVarList *desc);

void SaveFileStart(void);
void SaveFileDone(void);
void SaveFileError(void);
#endif /* SAVELOAD_H */
