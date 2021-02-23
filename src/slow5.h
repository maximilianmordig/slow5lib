// Header with slow5 file definitions
// TODO structure pack to min size
// TODO fix and add function descriptions

#ifndef SLOW5_H
#define SLOW5_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "klib/khash.h"
#include "klib/kvec.h"
#include "press.h"
#include "slow5_defs.h"

/* Formats */

// File formats to be dealing with
enum slow5_fmt {
    FORMAT_UNKNOWN,
    FORMAT_ASCII,
    FORMAT_BINARY
};

// SLOW5 file name with corresponding format
struct slow5_fmt_meta {
    const char *name;
    enum slow5_fmt format;
};
static const struct slow5_fmt_meta SLOW5_FORMAT_META[] = {
    { ASCII_NAME,   FORMAT_ASCII    },
    { BINARY_NAME,  FORMAT_BINARY   }
};

/* Header */

// SLOW5 versioning
struct slow5_version {
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
};

// Auxiliary attribute map: attribute string -> position
//KHASH_MAP_INIT_STR(s2i32, int32_t)

// SLOW5 auxiliary attribute metadata
struct slow5_aux_meta {
    uint32_t num;
    size_t cap;

    char **attrs;
    char **types;
    bool *is_ptr;
    uint8_t *sizes;

    //khash_t(s2i32) *attr_to_pos;
};

// Header data map: attribute string -> data string
KHASH_MAP_INIT_STR(s2s, char *)
// Header data attributes set
KHASH_SET_INIT_STR(s)

// SLOW5 header data
struct slow5_hdr_data {
    uint32_t num_attrs;	            // Number of data attributes
    khash_t(s) *attrs;              // Set of the data attributes
    kvec_t(khash_t(s2s) *) maps;    // Dynamic array of maps (attribute string -> data string) length=num_read_groups
};

// SLOW5 header
struct slow5_hdr {
	struct slow5_version version;
    uint32_t num_read_groups;           // Number of read groups
    struct slow5_hdr_data data;         // Header data
    struct slow5_aux_meta *aux_meta;    // Auxiliary field metadata
};

/* Read Record */

// SLOW5 main record columns
enum slow5_cols {
    SLOW5_COLS_FOREACH(GENERATE_ENUM)
    SLOW5_COLS_NUM
};

// SLOW5 auxiliary attributed data
struct slow5_rec_aux_data {
    uint64_t len;
    uint64_t bytes;
    char *type; // TODO decision: remove this and use slow5_aux_meta types but with hash map from attr -> position, or use this
    uint8_t *data;
};

// Header data map: auxiliary attribute string -> auxiliary data
KHASH_MAP_INIT_STR(s2a, struct slow5_rec_aux_data)

// SLOW5 record data
typedef uint64_t slow5_rec_size_t;
typedef uint16_t slow5_rid_len_t;
struct slow5_rec {
    slow5_rid_len_t read_id_len;
    SLOW5_COLS_FOREACH(GENERATE_STRUCT)
    khash_t(s2a) *aux_map;  // Auxiliary attribute string -> auxiliary data
};

/* SLOW5 file */

// SLOW5 file meta
struct slow5_file_meta {
    const char *pathname;
    int fd;
    uint64_t start_rec_offset;
};

// SLOW5 file structure
struct slow5_file {
    FILE *fp;
    enum slow5_fmt format;
    struct press *compress; // TODO better name
    struct slow5_hdr *header;
    struct slow5_idx *index;
    struct slow5_file_meta meta;
};


/* Public API */

/**
 * Open a slow5 file with a specific mode given it's pathname.
 *
 * Attempt to guess the file's slow5 format from the pathname's extension.
 * Return NULL if pathname or mode is NULL,
 * or if the pathname's extension is not recognised,
 * of if the pathname is invalid.
 *
 * Otherwise, return a slow5 file structure with the header parsed.
 * slow5_close() should be called when finished with the structure.
 *
 * @param   pathname    relative or absolute path to slow5 file
 * @param   mode        same mode as in fopen()
 * @return              slow5 file structure
 */
struct slow5_file *slow5_open(const char *pathname, const char *mode);

/**
 * Open a slow5 file of a specific format with a mode given it's pathname.
 *
 * Return NULL if pathname or mode is NULL, or if the format specified doesn't match the file.
 * slow5_open_with(pathname, mode, FORMAT_UNKNOWN) is equivalent to slow5_open(pathname, mode).
 *
 * Otherwise, return a slow5 file structure with the header parsed.
 * slow5_close() should be called when finished with the structure.
 *
 * @param   pathname    relative or absolute path to slow5 file
 * @param   mode        same mode as in fopen()
 * @param   format      format of the slow5 file
 * @return              slow5 file structure
 */
struct slow5_file *slow5_open_with(const char *pathname, const char *mode, enum slow5_fmt format);

// Return
// 0    success
// -1   input invalid
// -2   failure
int slow5_convert(struct slow5_file *from, FILE *to_fp, enum slow5_fmt to_format, press_method_t to_compress);

// Merge slow5 files to another slow5 file
// TODO Just a merge for 2 -> 1?
// TODO compile time 2 args in ...
int8_t slow5_merge(struct slow5_file *s5p_to, ...); // TODO
int8_t slow5_vmerge(struct slow5_file *s5p_to, va_list ap); // TODO

// Split a slow5 file to a dir
// TODO split into multiple slow5 files from same rg
int8_t slow5_split(const char *dirname_to, struct slow5_file *s5p_from); // TODO

/**
 * Close a slow5 file and free its memory.
 *
 * @param   s5p slow5 file structure
 * @return      same as fclose()
 */
int slow5_close(struct slow5_file *s5p);


/**
 * Create the index file for slow5 file.
 * Overrides if already exists.
 *
 * Return -1 on error,
 * 0 on success.
 *
 * @param   s5p slow5 file structure
 * @return  error codes described above
 */
int slow5_idx(struct slow5_file *s5p);


/**
 * Get an empty read structure.
 * To be freed with slow5_rec_free().
 *
 * @return  ptr to the record
 */
static inline struct slow5_rec *slow5_rec_init(void) {
    struct slow5_rec *read = (struct slow5_rec *) calloc(1, sizeof *read);

    return read;
}

/**
 * Get a read entry from a slow5 file corresponding to a read_id.
 *
 * Allocates memory for *read if it is NULL.
 * Otherwise, the data in *read is freed and overwritten.
 * slow5_rec_free() should always be called when finished with the structure.
 *
 * Creates the index if not already there
 *
 * Return
 * TODO are these error codes too much?
 *  0   the read was successfully found and stored
 * -1   read_id, read or s5p is NULL
 * -2   the index was not previously init and failed to init
 * -3   read_id was not found in the index
 * -4   reading error when reading the slow5 file
 * -5   parsing error
 *
 * @param   read_id the read identifier
 * @param   read    address of a slow5_rec pointer
 * @param   s5p     slow5 file
 * @return  error code described above
 */
int slow5_get(const char *read_id, struct slow5_rec **read, struct slow5_file *s5p);

/**
 * Get the read entry under the current file pointer of a slow5 file.
 *
 * Allocates memory for *read if it is NULL.
 * Otherwise, the data in *read is freed and overwritten.
 * slow5_rec_free() should always be called when finished with the structure.
 *
 * Return
 * TODO are these error codes too much?
 *  0   the read was successfully found and stored
 * -1   read_id, read or s5p is NULL
 * -2   reading error when reading the slow5 file
 * -3   parsing error
 *
 * @param   read    address of a slow5_rec pointer
 * @param   s5p     slow5 file
 * @return  error code described above
 */
int slow5_get_next(struct slow5_rec **read, struct slow5_file *s5p);

/**
 * Add a read entry to the slow5 file.
 *
 * Return
 *  0   the read was successfully stored
 * -1   read or s5p is NULL
 * -2   the index was not previously init and failed to init
 * -3   duplicate read id
 * -4   writing failure
 *
 * @param   read    slow5_rec ptr
 * @param   s5p     slow5 file
 * @return  error code described above
 */
int slow5_rec_add(struct slow5_rec *read, struct slow5_file *s5p);

/**
 * Remove a read entry at a read_id in a slow5 file.
 *
 * Return
 *  0   the read was successfully stored
 * -1   an input parameter is NULL
 * -2   the index was not previously init and failed to init
 * -3   read_id was not found in the index
 *
 * @param   read_id the read identifier
 * @param   s5p     slow5 file
 * @return  error code described above
 */
int slow5_rec_rm(const char *read_id, struct slow5_file *s5p); // TODO

int8_t slow5_rec_get_int8(const struct slow5_rec *read, const char *attr, int *err);
int16_t slow5_rec_get_int16(const struct slow5_rec *read, const char *attr, int *err);
int32_t slow5_rec_get_int32(const struct slow5_rec *read, const char *attr, int *err);
int64_t slow5_rec_get_int64(const struct slow5_rec *read, const char *attr, int *err);
uint8_t slow5_rec_get_uint8(const struct slow5_rec *read, const char *attr, int *err);
uint16_t slow5_rec_get_uint16(const struct slow5_rec *read, const char *attr, int *err);
uint32_t slow5_rec_get_uint32(const struct slow5_rec *read, const char *attr, int *err);
uint64_t slow5_rec_get_uint64(const struct slow5_rec *read, const char *attr, int *err);
float slow5_rec_get_float(const struct slow5_rec *read, const char *attr, int *err);
double slow5_rec_get_double(const struct slow5_rec *read, const char *attr, int *err);
char slow5_rec_get_char(const struct slow5_rec *read, const char *attr, int *err);
char *slow5_rec_get_str(const struct slow5_rec *read, const char *attr, int *err);
// TODO add other array types

/**
 * Get the read entry in the specified format.
 *
 * Returns NULL if read is NULL,
 * or format is FORMAT_UNKNOWN,
 * or the read attribute values are invalid
 *
 * @param   read        slow5_rec pointer
 * @param   format      slow5 format to write the entry in
 * @param   written     number of bytes written to the returned buffer
 * @param   compress    compress structure
 * @return  malloced string to use free() on, NULL on error
 */
void *slow5_rec_to_mem(struct slow5_rec *read, struct slow5_aux_meta *aux_meta, enum slow5_fmt format, struct press *compress, size_t *n);

/**
 * Print a read entry in the specified format to a file pointer.
 *
 * On success, the number of bytes written is returned.
 * On error, -1 is returned.
 *
 * @param   fp      output file pointer
 * @param   read    slow5_rec pointer
 * @param   format  slow5 format to write entry in
 * @param   compress
 * @return  number of bytes written, -1 on error
 */
int slow5_rec_fwrite(FILE *fp, struct slow5_rec *read, struct slow5_aux_meta *aux_meta, enum slow5_fmt format, struct press *compress);
static inline int slow5_rec_print(struct slow5_rec *read, struct slow5_aux_meta *aux_meta, enum slow5_fmt format, struct press *compress) {
    return slow5_rec_fwrite(stdout, read, aux_meta, format, compress);
}

// Free a read entry
void slow5_rec_free(struct slow5_rec *read);



/**
 * Get a header data attribute for a particular read_group.
 *
 * Returns NULL if the attribute name doesn't exist
 * or the read group is out of range
 * or an input parameter is NULL.
 *
 * @param   attr        attribute name
 * @param   read_group  the read group
 * @param   header      slow5 header
 * @return  the attribute's value, or NULL on error
 */
char *slow5_hdr_get(const char *attr, uint32_t read_group, const struct slow5_hdr *header);

/**
 * Add a new header data attribute.
 *
 * All values are set to NULL for each read group.
 *
 * Returns -1 if an input parameter is NULL.
 * Returns -2 if the attribute already exists.
 * Returns 0 other.
 *
 * @param   attr    attribute name
 * @param   header  slow5 header
 * @return  0 on success, <0 on error as described above
 */
int slow5_hdr_add_attr(const char *attr, struct slow5_hdr *header);

/**
 * Add a new header read group.
 *
 * All values are set to NULL for the new read group.
 *
 * Returns -1 if an input parameter is NULL.
 * Returns the new read group number otherwise.
 *
 * @param   header  slow5 header
 * @return  < 0 on error as described above
 */
// TODO check return type but should be large enough to return -1 and the largest read group
int64_t slow5_hdr_add_rg(struct slow5_hdr *header);

/**
 * Set a header data attribute for a particular read_group.
 *
 * Doesn't take memory ownership of the value given.
 *
 * Returns -1 if the attribute name doesn't exist
 * or the read group is out of range
 * or an input parameter is NULL.
 * Returns 0 other.
 *
 * @param   attr        attribute name
 * @param   value       new attribute value
 * @param   read_group  the read group
 * @param   header      slow5 header
 * @return  0 on success, -1 on error
 */
int slow5_hdr_set(const char *attr, const char *value, uint32_t read_group, struct slow5_hdr *header);

/**
 * Get the header in the specified format.
 *
 * Returns NULL if s5p is NULL
 * or format is FORMAT_UNKNOWN
 * or an internal error occurs.
 *
 * @param   header  slow5 header
 * @param   format  slow5 format to write the entry in
 * @param   comp    compression method
 * @param   written number of bytes written to the returned buffer
 * @return  malloced memory storing the slow5 header representation,
 *          to use free() on afterwards
 */
void *slow5_hdr_to_mem(struct slow5_hdr *header, enum slow5_fmt format, press_method_t comp, size_t *written);

/**
 * Print the header in the specified format to a file pointer.
 *
 * On success, the number of bytes written is returned.
 * On error, -1 is returned.
 *
 * @param   fp      output file pointer
 * @param   header  slow5 header
 * @param   format  slow5 format to write the entry in
 * @return  number of bytes written, -1 on error
 */
int slow5_hdr_fwrite(FILE *fp, struct slow5_hdr *header, enum slow5_fmt format, press_method_t comp);
static inline int slow5_hdr_print(struct slow5_hdr *header, enum slow5_fmt format, press_method_t comp) {
    return slow5_hdr_fwrite(stdout, header, format, comp);
}

/**
 * Print the binary end of file to a file pointer.
 *
 * On success, the number of bytes written is returned.
 * On error, -1 is returned.
 *
 * @param   fp      output file pointer
 * @return  number of bytes written, -1 on error
 */
ssize_t slow5_eof_fwrite(FILE *fp);
static inline ssize_t slow5_eof_print(void) {
    return slow5_eof_fwrite(stdout);
}

#endif
