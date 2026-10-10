#ifndef MATLOG_DOCS_EXAMPLES_H
#define MATLOG_DOCS_EXAMPLES_H

#include <matlog/docs/docs.h>

/*
 * The examples (plan section 4.16): worked sessions in examples/*.mlog,
 * compiled into the program (source/matlog/docs/examples_data.c, made by
 * tools/gen_docs.py). An example is a session file whose notes explain the
 * steps:
 *
 *	# The title of the example           the first note
 *	# One line on what it shows          the second note
 *	# ... more notes and math lines
 *
 * The file name is `area-name.mlog`; the word before the first dash puts it
 * in a group of the table of contents (basics, algebra, calculus, linalg,
 * units, logic, lambda, robotics). The name without `.mlog` is what `:example`
 * and `matlog examples` take.
 *
 * Every example is a page of the documentation too, with the id `ex-` and its
 * name: the notes are paragraphs, the math lines are code with the results
 * as the evaluator gives them now, so a page cannot disagree with the program.
 * The page `examples` is the table of contents. ml_docs_load adds them.
 */

extern const MlDocSource ml_example_sources [];
extern const uint64_t    ml_example_source_count;

uint64_t ml_example_count (void);

/* The name of example i (`calculus-newton-sqrt2`) and its file text. */
MlSlice ml_example_name (uint64_t i);
MlSlice ml_example_text (uint64_t i);

/* The index of the example with the name; ml_example_count () when none. */
uint64_t ml_example_find (MlSlice name);

/* The title (first note) and the summary (second note) of example i. */
MlResult ml_example_title (MlStr *out, uint64_t i);
MlResult ml_example_summary (MlStr *out, uint64_t i);

/* The group of example i (`Calculus`), or the first word of its name. */
MlSlice ml_example_area (uint64_t i);

/* The text of the documentation page of example i. */
MlResult ml_example_page (MlStr *out, uint64_t i);

/* The text of the page `examples`: the groups with a link to each example. */
MlResult ml_examples_contents (MlStr *out);

/* Adds the table of contents and a page for each example to d. */
MlResult ml_examples_add_pages (MlDocs *d);

#endif /* MATLOG_DOCS_EXAMPLES_H */
