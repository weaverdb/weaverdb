/* Stub access/reloptions.h for pgvector */
#ifndef ACCESS_RELOPTIONS_H
#define ACCESS_RELOPTIONS_H

typedef struct relopt_enum_elt_def relopt_enum_elt_def;
typedef struct relopt_gen relopt_gen;

typedef int relopt_kind;

static inline relopt_kind add_reloption_kind(void) { return 1; }

static inline void add_int_reloption(relopt_kind kind, const char *name, const char *desc,
									 int default_val, int min, int max, int scope)
{ (void)kind; (void)name; (void)desc; (void)default_val; (void)min; (void)max; (void)scope; }

static inline void add_real_reloption(relopt_kind kind, const char *name, const char *desc,
									  double default_val, double min, double max, int scope)
{ (void)kind; (void)name; (void)desc; (void)default_val; (void)min; (void)max; (void)scope; }

static inline void add_string_reloption(relopt_kind kind, const char *name, const char *desc,
										const char *default_val, int scope)
{ (void)kind; (void)name; (void)desc; (void)default_val; (void)scope; }

typedef struct relopt_parse_elt
{
	const char *name;
	int			type;
	int			offset;
} relopt_parse_elt;

#define RELOPT_TYPE_INT 1
#define RELOPT_TYPE_REAL 2
#define RELOPT_TYPE_STRING 3
#define RELOPT_TYPE_BOOL 4

static inline bytea *
build_reloptions(Datum reloptions, bool validate, relopt_kind kind, Size relopt_struct_size,
				 const relopt_parse_elt *relopt_elt, int num_relopt_elt)
{
	(void)reloptions; (void)validate; (void)kind; (void)relopt_struct_size; (void)relopt_elt; (void)num_relopt_elt;
	return (bytea *) 0;
}

#endif /* ACCESS_RELOPTIONS_H */